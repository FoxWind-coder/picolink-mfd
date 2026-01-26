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
    usb_packet_t *pkt;
    spi_config_t *scfg;
    int ret;

    pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
    if (!pkt)
        return -ENOMEM;

    scfg = (spi_config_t *)pkt->payload;

    pkt->header.type = CMD_TYPE_CONFIG;
    pkt->header.iface_idx = IFACE_SPI;
    pkt->header.length = sizeof(spi_config_t);

    /* Use 0xFF for pins to tell Pico firmware not to change pin mapping */
    scfg->sck_pin = 0xFF;
    scfg->mosi_pin = 0xFF;
    scfg->miso_pin = 0xFF;
    memset(scfg->cs_pins, 0xFF, 4);

    scfg->baudrate = spi->max_speed_hz;
    
    /* SPI mode: bit 0 = CPHA, bit 1 = CPOL */
    scfg->mode = (uint8_t)(spi->mode & (SPI_CPOL | SPI_CPHA));

    dev_info(&pspi->mfd->udev->dev, 
             "SPI Setup: speed=%u, mode=%u, CS=%d\n", 
             scfg->baudrate, scfg->mode, 
#if KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE
             spi->chip_select[0]
#else
             spi->chip_select
#endif
    );

    /* Send packet without waiting for response (rx_pkt = NULL) */
    ret = picolink_send_packet(pspi->mfd->udev, 
                               pspi->mfd->bulk_out_endpointAddr, 
                               pkt, sizeof(*pkt));

    kfree(pkt);

    if (ret < 0) {
        dev_err(&pspi->mfd->udev->dev, "SPI Setup USB Error: %d\n", ret);
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
    usb_packet_t *pkt_out, *pkt_in;
    int ret;

    pkt_out = kzalloc(sizeof(*pkt_out), GFP_KERNEL);
    pkt_in = kzalloc(sizeof(*pkt_in), GFP_KERNEL);
    if (!pkt_out || !pkt_in) {
        ret = -ENOMEM;
        goto out;
    }

    pkt_out->header.type = CMD_TYPE_DATA;
    pkt_out->header.iface_idx = IFACE_SPI;
    pkt_out->header.length = xfer->len + 1;

#if KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE
    pkt_out->payload[0] = (uint8_t)spi->chip_select[0];
#else
    pkt_out->payload[0] = (uint8_t)spi->chip_select;
#endif
    
    if (xfer->tx_buf)
        memcpy(&pkt_out->payload[1], xfer->tx_buf, xfer->len);

    // dev_info(&pspi->mfd->udev->dev, "SPI TX: len=%u, cs=%d, first_byte=0x%02x\n", 
    //         xfer->len, pkt_out->payload[0], pkt_out->payload[1]);

    ret = picolink_transfer(pspi->mfd, pkt_out, pkt_in);

    if (ret) {
        dev_err(&pspi->mfd->udev->dev, "SPI Transfer USB Error: %d\n", ret);
    }
    
    if (ret == 0 && xfer->rx_buf && pkt_in->header.length > 0) {
        memcpy(xfer->rx_buf, pkt_in->payload, xfer->len);
    }

out:
    kfree(pkt_out);
    kfree(pkt_in);
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

    /* Programmatically set driver override to allow automatic spidev binding */
    spi->driver_override = kstrdup("spidev", GFP_KERNEL);

    if (device_attach(&spi->dev) < 0) {
        dev_warn(&mfd->udev->dev, "SPI: device_attach failed for spi%d.%d\n", 
                 info.bus_num, index);
    }

    g_spi->cs_devs[index] = spi;
    dev_info(&mfd->udev->dev, "SPI: Device spi%d.%d registered automatically\n", 
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