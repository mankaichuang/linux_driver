#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/cdev.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include "ap3216creg.h"

#define AP3216C_CNT       1
#define AP3216C_NAME      "ap3216c"

/*ap3216c设备结构体*/
struct ap3216c_dev{
    dev_t   devid;              /*设备号*/
    struct cdev cdev;           /*cdev*/
    struct class *class;        /*类*/
    struct device *device;      /*设备*/
    int major;                  /*主设备号*/
    int minor;                  /*次设备号*/
    unsigned short ir,ps,als;

    void *private_data;         /*私有数据域*/
}; 

struct ap3216c_dev ap3216cdev;


/*读取AP3216C的寄存器值*/
static int ap3216c_read_reg(struct ap3216c_dev *dev, u8 reg, void *val, int len)
{
    struct i2c_msg msg[2];
    struct i2c_client *client = (struct i2c_client *)dev->private_data;

    /*构建msg*/
    /*第一步写入要读的寄存器地址*/
    msg[0].addr = client->addr;             /*i2c设备地址*/
    msg[0].flags = 0;                       /*写操作标志*/
    msg[0].buf = &reg;                      /*要写入的数据，这里是寄存器地址*/
    msg[0].len = 1;                         /*要写入的数据长度，这里寄存器地址是1个字节*/

    /*第二步发送要读的寄存器地址，并将读取到的值保存到msg.buf中*/
    msg[1].addr = client->addr;             /*i2c设备地址*/             
    msg[1].flags = I2C_M_RD;                /*读操作标志*/
    msg[1].buf = val;                       /*将读出的数据保存在val中*/
    msg[1].len = len;                       /*读出的数据长度*/

    return i2c_transfer(client->adapter, msg, 2);
}

/*从寄存器读取一个数据*/
static u8 ap3216c_readone(struct ap3216c_dev *dev, u8 reg)
{
    u8 data;
    ap3216c_read_reg(dev, reg, &data, 1);
    return data;
}


/*向AP3216C的寄存器写数据*/
static int ap3216c_write_reg(struct ap3216c_dev *dev, u8 reg, u8 *buf, u8 len)
{
    u8 buffer[256];
    struct i2c_msg msg;
    struct i2c_client *client = (struct i2c_client *)dev->private_data;

    /*初始化写入数据包，数据的第一个字写为写入的地址*/
    buffer[0] = reg;
    memcpy(&buffer[1], buf, len);

    /*构建msg*/
    msg.addr = client->addr;             /*i2c设备地址*/
    msg.flags = 0;                       /*写操作标志*/
    msg.buf = buffer;                    /*要写入的数据, 寄存器地址+buf数据*/
    msg.len = len + 1;                   /*要写入的数据长度*/

    return i2c_transfer(client->adapter, &msg, 1);
}

/*向寄存器写入一个数据*/
static void ap3216c_writeone(struct ap3216c_dev *dev, u8 reg, u8 data)
{
    u8 buf;
    buf = data;
    ap3216c_write_reg(dev, reg, &buf, 1);
}

/* AP3216C数据读取 */
static void ap3216c_readdata(struct ap3216c_dev *dev)
{
    unsigned char buf[6];
    unsigned char i = 0;

    /* 循环的读取数据 */
    for(i = 0; i < 6; i++) {
        buf[i] = ap3216c_readone(dev, AP3216C_IRDATALOW + i);
    }

    if(buf[0] & 0x80) { /* 为真表示IR和PS数据无效 */
        dev->ir = 0;
        dev->ps = 0;
    } else {
        dev->ir = ((unsigned short)buf[1] << 2) | (buf[0] & 0x03);
        dev->ps = (((unsigned short)buf[5] & 0x3F) << 4) | (buf[4] & 0x0F);
    }

    dev->als  = ((unsigned short)buf[3] << 8) | buf[2]; 
}

static int ap3216c_open (struct inode *inode, struct file *filp)
{
    u8 value = 0;
    filp->private_data = &ap3216cdev;
    printk("ap3216c_open\r\n");

    /*AP3216C传感器初始化 */
    ap3216c_writeone(&ap3216cdev, AP3216C_SYSTEMCONG, 0X4); /* 复位 */
    mdelay(50);
    ap3216c_writeone(&ap3216cdev, AP3216C_SYSTEMCONG, 0X3); /* 复位 */
    value = ap3216c_readone(&ap3216cdev, AP3216C_SYSTEMCONG);
    printk("ap3216c system config reg=%#x\r\n", value);
    return 0;
}

static ssize_t ap3216c_read (struct file *filp, char __user *buf, size_t cnt, loff_t *off_t)
{
    short data[3];
    long err = 0;
    struct ap3216c_dev *dev = (struct ap3216c_dev *)filp->private_data;

    ap3216c_readdata(dev);

    data[0] = dev->ir;
    data[1] = dev->als;
    data[2] = dev->ps;
    // printk("driver ir = %d, als = %d, ps = %d\r\n", dev->ir, dev->als, dev->ps);

    err = copy_to_user(buf, data, sizeof(data));

    return 0;
}

static int ap3216c_release (struct inode *inode, struct file *filp)
{
    printk("ap3216c_release\r\n");
    return 0;
}
/*字符设备操作集*/
static struct file_operations ap3216c_fops = {
    .owner	 = THIS_MODULE,
    .open	 = ap3216c_open,
    .read	 = ap3216c_read,
    .release = ap3216c_release
};

/*传统匹配表*/
static const struct i2c_device_id ap3216c_id_table[] = {
	{ "alk,ap3216c", 0 },
	{}
};

/*设备树匹配表*/
static const struct of_device_id ap3216c_of_match[] = {
	{ .compatible = "alk,ap3216c", },
	{},
};


static int ap3216c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret = 0;
    printk("ap3216c_probe!\r\n");

    /*注册字符设备驱动*/
    ap3216cdev.major = 0;   /*设置主设备号为0，确保让系统分配设备号*/
    /*1、创建设备号*/
    if(ap3216cdev.major)         /*如果定义了设备号*/
    {
        ap3216cdev.devid = MKDEV(ap3216cdev.major, 0);
        ret = register_chrdev_region(ap3216cdev.devid, AP3216C_CNT, AP3216C_NAME);
    }
    else                        /*如果没有定义设备号*/
    {
        ret = alloc_chrdev_region(&ap3216cdev.devid, 0, AP3216C_CNT, AP3216C_NAME); /*申请设备号*/
        ap3216cdev.major = MAJOR(ap3216cdev.devid);
        ap3216cdev.minor = MINOR(ap3216cdev.devid);
    }
    if(ret < 0)
    {
        goto fail_devid;
    }
    printk("ap3216cdev major=%d, minor=%d\r\n", ap3216cdev.major, ap3216cdev.minor);

    /*2初始话cdev*/
    ap3216cdev.cdev.owner = THIS_MODULE;
    cdev_init(&ap3216cdev.cdev, &ap3216c_fops);

    /*3、添加一个cdev*/
    ret = cdev_add(&ap3216cdev.cdev, ap3216cdev.devid, AP3216C_CNT);
    if(ret < 0)
    {
        goto fail_cdev;
    }

    /*4、创建类*/
    ap3216cdev.class = class_create(THIS_MODULE, AP3216C_NAME);
    if(IS_ERR(ap3216cdev.class))
    {
        ret = PTR_ERR(ap3216cdev.class);
        goto fail_class;
    }
    /*5、创建设备*/
    ap3216cdev.device = device_create(ap3216cdev.class, NULL, ap3216cdev.devid, NULL, AP3216C_NAME);
    if(IS_ERR(ap3216cdev.device))
    {
        ret = PTR_ERR(ap3216cdev.device);
        goto fail_device;
    }

    ap3216cdev.private_data = client;           /*将client数据保存在设备私有数据域中*/

    printk("ap3216cdev init()\r\n");
    return 0;

fail_device:
    class_destroy(ap3216cdev.class);
fail_class:
    cdev_del(&ap3216cdev.cdev);
fail_cdev:
    unregister_chrdev_region(ap3216cdev.devid, AP3216C_CNT);   
fail_devid:
    return ret;
}

static int ap3216c_remove(struct i2c_client *client)
{
    /*注销设备驱动*/
    cdev_del(&ap3216cdev.cdev);
    unregister_chrdev_region(ap3216cdev.devid, AP3216C_CNT);
    device_destroy(ap3216cdev.class, ap3216cdev.devid);
    class_destroy(ap3216cdev.class);

    printk("ap3216c_remove!\r\n");
    return 0;
}
/*ap3216c driver结构体*/
static struct i2c_driver ap3216c_driver = {
	.driver = {
		.name = "ap3216c",
		.owner = THIS_MODULE,
        .of_match_table = ap3216c_of_match,
	},
	.probe = ap3216c_probe,
	.remove = ap3216c_remove,
	.id_table = ap3216c_id_table,
};

static int __init ap3216c_init(void)
{
    return i2c_add_driver(&ap3216c_driver);
}

static void __exit ap3216c_exit(void)
{
    i2c_del_driver(&ap3216c_driver);
}


module_init(ap3216c_init);
module_exit(ap3216c_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("mankc");