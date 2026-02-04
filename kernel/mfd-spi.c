// mfd-spi.c
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/property.h>
#include "picolink.h"

#if KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE
    #define REMOVE_RET_TYPE int
    #define REMOVE_RET_VAL  0
#else
    #define REMOVE_RET_TYPE void
    #define REMOVE_RET_VAL
#endif

struct picolink_spi {
    struct spi_controller *ctlr;
    struct picolink_dev *mfd;
    struct spi_device *cs_devs[4];
};

static int picolink_spi_setup(struct spi_device *spi)
{
    struct picolink_spi *pspi = spi_controller_get_devdata(spi->controller);
    struct picolink_dev *mfd = pspi->mfd;
    usb_packet_t pkt_out;
    spi_config_t *scfg;
    int ret;

    memset(&pkt_out, 0, sizeof(pkt_out));
    scfg = (spi_config_t *)pkt_out.payload;

    pkt_out.header.type = CMD_TYPE_CONFIG;
    pkt_out.header.iface_idx = IFACE_SPI;
    pkt_out.header.length = sizeof(spi_config_t);

    // Keep default pin assignments
    scfg->sck_pin = 0xFF; 
    scfg->mosi_pin = 0xFF;
    scfg->miso_pin = 0xFF;
    memset(scfg->cs_pins, 0xFF, 4);

    scfg->baudrate = spi->max_speed_hz;
    scfg->mode = (uint8_t)(spi->mode & (SPI_CPOL | SPI_CPHA));

    /* Using picolink_transfer for mutex-protected atomic USB transaction */
    ret = picolink_transfer(mfd, &pkt_out, mfd->transfer_rx_buf);

    if (ret < 0) {
        dev_err(&mfd->udev->dev, "SPI Setup Failed: %d\n", ret);
        return ret;
    }

    return 0;
}

static struct picolink_spi *g_spi = NULL;

int picolink_spi_add_device(struct picolink_dev *mfd, int index, int pin);
void picolink_spi_remove_device(int index);

static int picolink_spi_transfer_one(struct spi_controller *ctlr, struct spi_device *spi, struct spi_transfer *xfer)
{
    struct picolink_spi *pspi = spi_controller_get_devdata(ctlr);
    struct picolink_dev *mfd = pspi->mfd;
    int ret = 0;
    u32 total_len = xfer->len;
    u32 sent = 0;
    u8 cs_idx;

#if KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE
    cs_idx = (uint8_t)spi->chip_select[0];
#else
    cs_idx = (uint8_t)spi->chip_select;
#endif

    while (sent < total_len) {
        /* Use fixed 32-byte step for Pico firmware stability */
        u32 chunk_len = (total_len - sent > 32) ? 32 : (total_len - sent);
        
        // 1. Prepare packet in MFD internal buffer
        memset(mfd->transfer_tx_buf, 0, sizeof(usb_packet_t));
        
        mfd->transfer_tx_buf->header.type = CMD_TYPE_DATA;
        mfd->transfer_tx_buf->header.iface_idx = IFACE_SPI;
        
        /* Payload length: 1 byte (CS control) + actual data */
        mfd->transfer_tx_buf->header.length = chunk_len + 1;
        
        // CS Logic: Flag 0x80 tells Pico NOT to de-assert CS after this packet
        mfd->transfer_tx_buf->payload[0] = cs_idx;
        if (sent + chunk_len < total_len) {
            mfd->transfer_tx_buf->payload[0] |= 0x80; 
        }

        // Copy TX data
        if (xfer->tx_buf)
            memcpy(&mfd->transfer_tx_buf->payload[1], xfer->tx_buf + sent, chunk_len);
        else
            memset(&mfd->transfer_tx_buf->payload[1], 0, chunk_len);

        // 2. Atomic USB transfer
        ret = picolink_transfer(mfd, mfd->transfer_tx_buf, mfd->transfer_rx_buf);
        
        if (ret < 0) {
            dev_err(&mfd->udev->dev, "SPI USB Transfer Failed: %d (sent %d/%d)\n", 
                    ret, sent, total_len);
            break;
        }

        // 3. Copy received RX data to Linux buffer
        if (xfer->rx_buf) {
            memcpy(xfer->rx_buf + sent, mfd->transfer_rx_buf->payload, chunk_len);
        }

        sent += chunk_len;
    }

    spi_finalize_current_transfer(ctlr);
    return ret;
}



int picolink_spi_add_device(struct picolink_dev *mfd, int index, int pin)
{
    struct spi_device *spi;
    struct spi_board_info info = {
        .modalias = "spidev",
        .max_speed_hz = 1000000,
        .bus_num = g_spi->ctlr->bus_num,
        .chip_select = index,
        .mode = SPI_MODE_0,
    };
    
    if (!g_spi || index < 0 || index > 3) 
        return -EINVAL;
        
    if (g_spi->cs_devs[index]) 
        return -EBUSY;

    spi = spi_new_device(g_spi->ctlr, &info);
    if (!spi) {
        dev_err(&mfd->udev->dev, "SPI: Failed to create device\n");
        return -ENOMEM;
    }

    /* Set driver override to allow automatic spidev binding */
    spi->driver_override = kstrdup("spidev", GFP_KERNEL);

    if (device_attach(&spi->dev) < 0) {
        dev_warn(&mfd->udev->dev, "SPI: device_attach failed for spi%d.%d\n", 
                 info.bus_num, index);
    }

    g_spi->cs_devs[index] = spi;
    dev_info(&mfd->udev->dev, "SPI: Device spi%d.%d registered\n", 
             g_spi->ctlr->bus_num, index);
             
    return 0;
}
EXPORT_SYMBOL_GPL(picolink_spi_add_device);

void picolink_spi_remove_device(int index)
{
    if (index >= 0 && index < 4 && g_spi && g_spi->cs_devs[index]) {
        spi_unregister_device(g_spi->cs_devs[index]);
        g_spi->cs_devs[index] = NULL;
    }
}
EXPORT_SYMBOL_GPL(picolink_spi_remove_device);

static int picolink_spi_probe(struct platform_device *pdev)
{
    struct spi_controller *ctlr;
    struct picolink_spi *pspi;
    int ret;

    ctlr = spi_alloc_master(&pdev->dev, sizeof(*pspi));
    if (!ctlr)
        return -ENOMEM;

    pspi = spi_controller_get_devdata(ctlr);
    pspi->ctlr = ctlr;
    pspi->mfd = dev_get_drvdata(pdev->dev.parent);
    
    if (!pspi->mfd) {
        dev_err(&pdev->dev, "Failed to get MFD data\n");
        spi_controller_put(ctlr);
        return -EINVAL;
    }

    ctlr->bus_num = -1; 
    ctlr->num_chipselect = 4;
    ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LSB_FIRST;
    ctlr->setup = picolink_spi_setup;
    ctlr->transfer_one = picolink_spi_transfer_one;
    ctlr->dev.of_node = pdev->dev.of_node;

    g_spi = pspi;
    platform_set_drvdata(pdev, pspi);
    
    ret = spi_register_controller(ctlr);
    if (ret) {
        dev_err(&pdev->dev, "Failed to register SPI controller: %d\n", ret);
        spi_controller_put(ctlr);
        return ret;
    }

    dev_info(&pdev->dev, "PicoLink SPI Controller registered (bus %d)\n", ctlr->bus_num);
    return 0;
}

static REMOVE_RET_TYPE picolink_spi_remove(struct platform_device *pdev)
{
    struct picolink_spi *pspi = platform_get_drvdata(pdev);
    int i;

    for (i = 0; i < 4; i++) {
        if (pspi->cs_devs[i]) 
            spi_unregister_device(pspi->cs_devs[i]);
    }

    spi_unregister_controller(pspi->ctlr);
    g_spi = NULL;
    return REMOVE_RET_VAL;
}

struct platform_driver picolink_spi_driver = {
    .driver = { .name = "picolink-spi" },
    .probe = picolink_spi_probe,
    .remove = picolink_spi_remove,
};
EXPORT_SYMBOL_GPL(picolink_spi_driver);