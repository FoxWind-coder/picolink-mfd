#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "picolink.h"

struct picolink_i2c {
    struct platform_device *pdev;
    struct i2c_adapter adap;
};

static int picolink_i2c_usb_xfer(struct picolink_i2c *pi2c, struct i2c_msg *msg)
{
    struct picolink_dev *mfd_core = dev_get_drvdata(pi2c->pdev->dev.parent);
    usb_packet_t *pkt_out, *pkt_in;
    int ret;
    
    pkt_out = kzalloc(sizeof(*pkt_out), GFP_KERNEL);
    pkt_in = kzalloc(sizeof(*pkt_in), GFP_KERNEL);
    if (!pkt_out || !pkt_in) {
        ret = -ENOMEM;
        goto out;
    }

    /* Prepare the request packet */
    pkt_out->header.iface_idx = IFACE_I2C;
    if (msg->flags & I2C_M_RD) {
        pkt_out->header.type = CMD_TYPE_READ;
        pkt_out->header.length = 2;
        pkt_out->payload[0] = msg->addr;
        pkt_out->payload[1] = msg->len;
    } else {
        pkt_out->header.type = CMD_TYPE_DATA;
        pkt_out->header.length = msg->len + 1;
        pkt_out->payload[0] = msg->addr;
        if (msg->len > 0) memcpy(&pkt_out->payload[1], msg->buf, msg->len);
    }

    /* Synchronous transfer: sends packet and waits for Pico response */
    ret = picolink_transfer(mfd_core, pkt_out, pkt_in);

    /* Analyze the transfer result */
    if (ret == 0) {
        if (pkt_in->header.type == CMD_TYPE_RESP && pkt_in->header.iface_idx == IFACE_I2C) {
            if (pkt_in->header.length > 0) {
                if (msg->flags & I2C_M_RD) {
                    memcpy(msg->buf, pkt_in->payload, msg->len);
                }
                ret = 0; 
            } else {
                ret = -ENXIO; /* NACK: device did not respond */
            }
        } else {
            ret = -EPROTO; 
        }
    }

out:
    kfree(pkt_out);
    kfree(pkt_in);
    return ret;
}

static int picolink_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
    struct picolink_i2c *pi2c = i2c_get_adapdata(adap);
    int i, ret;
    for (i = 0; i < num; i++) {
        ret = picolink_i2c_usb_xfer(pi2c, &msgs[i]);
        if (ret < 0) return ret;
    }
    return num;
}

static u32 picolink_i2c_func(struct i2c_adapter *adap)
{
    return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm picolink_i2c_algo = {
    .master_xfer   = picolink_i2c_xfer,
    .functionality = picolink_i2c_func,
};

static int picolink_i2c_probe(struct platform_device *pdev)
{
    struct picolink_i2c *pi2c;
    // struct picolink_dev *mfd_core = dev_get_drvdata(pdev->dev.parent);
    // usb_packet_t *cfg_pkt;
    int ret;

    pi2c = devm_kzalloc(&pdev->dev, sizeof(*pi2c), GFP_KERNEL);
    if (!pi2c) return -ENOMEM;

    pi2c->pdev = pdev;
    pi2c->adap.owner = THIS_MODULE;
    pi2c->adap.algo = &picolink_i2c_algo;
    pi2c->adap.dev.parent = &pdev->dev;
    strscpy(pi2c->adap.name, "PicoLink I2C", sizeof(pi2c->adap.name));
    
    i2c_set_adapdata(&pi2c->adap, pi2c);
    platform_set_drvdata(pdev, pi2c);

    ret = i2c_add_adapter(&pi2c->adap);
    if (ret) return ret;

    // msleep(100); 
    // cfg_pkt = kzalloc(sizeof(*cfg_pkt), GFP_KERNEL);
    // if (cfg_pkt) {
    //     cfg_pkt->header.type = CMD_TYPE_CONFIG;
    //     cfg_pkt->header.iface_idx = IFACE_I2C;
    //     cfg_pkt->header.length = 2;
    //     cfg_pkt->payload[0] = 4;
    //     cfg_pkt->payload[1] = 5;
    //     picolink_send_packet(mfd_core->udev, mfd_core->bulk_out_endpointAddr, cfg_pkt, sizeof(*cfg_pkt));
    //     kfree(cfg_pkt);
    // }
    return 0;
}

static int picolink_i2c_remove(struct platform_device *pdev)
{
    struct picolink_i2c *pi2c = platform_get_drvdata(pdev);
    if (pi2c) i2c_del_adapter(&pi2c->adap);
    return 0;
}

struct platform_driver picolink_i2c_driver = {
    .driver = { .name = "picolink-i2c" },
    .probe  = picolink_i2c_probe,
    .remove = picolink_i2c_remove,
};
EXPORT_SYMBOL_GPL(picolink_i2c_driver);
MODULE_LICENSE("GPL");