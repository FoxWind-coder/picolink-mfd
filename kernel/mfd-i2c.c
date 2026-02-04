// mfd_i2c.c
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "picolink.h"

#if KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE
    #define REMOVE_RET_TYPE int
    #define REMOVE_RET_VAL  0
#else
    #define REMOVE_RET_TYPE void
    #define REMOVE_RET_VAL
#endif

struct picolink_i2c {
    struct platform_device *pdev;
    struct i2c_adapter adap;
};

static int picolink_i2c_usb_xfer(struct picolink_i2c *pi2c, struct i2c_msg *msg)
{
    struct picolink_dev *mfd_core = dev_get_drvdata(pi2c->pdev->dev.parent);
    usb_packet_t pkt_out; 
    usb_packet_t pkt_in;  
    int ret;
    int copy_len;
    
    if (!mfd_core || mfd_core->disconnected)
        return -ENODEV;

    memset(&pkt_out, 0, sizeof(pkt_out));
    pkt_out.header.iface_idx = IFACE_I2C;

    // 1. PACKET PREPARATION (REQUEST)
    if (msg->flags & I2C_M_RD) {
        pkt_out.header.type = CMD_TYPE_READ;
        pkt_out.header.length = 2;
        pkt_out.payload[0] = msg->addr;
        pkt_out.payload[1] = msg->len;
    } else {
        pkt_out.header.type = CMD_TYPE_DATA;
        // Limit length to payload capacity (60 bytes)
        copy_len = (msg->len > 60) ? 60 : msg->len; 
        
        pkt_out.header.length = copy_len + 1;
        pkt_out.payload[0] = msg->addr;
        if (copy_len > 0) {
            memcpy(&pkt_out.payload[1], msg->buf, copy_len);
        }
    }

    // 2. TRANSMISSION
    ret = picolink_transfer(mfd_core, &pkt_out, &pkt_in);

    // 3. RESULT PROCESSING (RESPONSE)
    if (ret == 0) {
        if (pkt_in.header.type == CMD_TYPE_RESP) {

            if (pkt_in.header.length == 0) {
                // Length 0 indicates an I2C error occurred on the Pico side
                return -ENXIO; // No such device or address
            }
            
            // If reading, copy data FROM Pico TO system buffer
            if (msg->flags & I2C_M_RD) {
                copy_len = (msg->len > 60) ? 60 : msg->len;
                memcpy(msg->buf, pkt_in.payload, copy_len);
            }
        } else {
            dev_err(&mfd_core->interface->dev, "I2C: Unexpected response type 0x%x\n", pkt_in.header.type);
            ret = -EIO;
        }
    }

    // Return 1 on success (number of messages processed)
    return (ret == 0) ? 1 : ret;
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

    return 0;
}

static REMOVE_RET_TYPE picolink_i2c_remove(struct platform_device *pdev)
{
    struct picolink_i2c *pi2c = platform_get_drvdata(pdev);
    if (pi2c) i2c_del_adapter(&pi2c->adap);
    return REMOVE_RET_VAL;
}

struct platform_driver picolink_i2c_driver = {
    .driver = { .name = "picolink-i2c" },
    .probe  = picolink_i2c_probe,
    .remove = picolink_i2c_remove,
};
EXPORT_SYMBOL_GPL(picolink_i2c_driver);

MODULE_LICENSE("GPL");