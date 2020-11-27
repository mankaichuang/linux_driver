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
#include <linux/spi/spi.h>

#include "icm20608reg.h"

#define icm20608_CNT       1
#define icm20608_NAME      "icm20608"

/*icm20608设备结构体*/
struct icm20608_dev{
    dev_t   devid;              /*设备号*/
    struct cdev cdev;           /*cdev*/
    struct class *class;        /*类*/
    struct device *device;      /*设备*/
    int major;                  /*主设备号*/
    int minor;                  /*次设备号*/
    void *private_data;         /*私有数据域*/
    struct device_node *nd;     /*设备节点*/

    int cs_gpio;                /* 片选所使用的 GPIO 编号*/
    signed int gyro_x_adc;      /* 陀螺仪 X 轴原始值 */
    signed int gyro_y_adc;      /* 陀螺仪 Y 轴原始值 */
    signed int gyro_z_adc;      /* 陀螺仪 Z 轴原始值 */
    signed int accel_x_adc;     /* 加速度计 X 轴原始值 */
    signed int accel_y_adc;     /* 加速度计 Y 轴原始值 */
    signed int accel_z_adc;     /* 加速度计 Z 轴原始值 */
    signed int temp_adc;        /* 温度原始值 */

}; 

struct icm20608_dev icm20608dev;

static int icm20608_read_regs(struct icm20608_dev *dev, u8 reg, void *buf, int len)
{
    int ret;
    u8 txdata[len];
    struct spi_message msg;
    struct spi_transfer *t;
    struct spi_device *spi = (struct spi_device *)dev->private_data;

    /*片选拉低，选中ICM20608*/
    gpio_set_value(dev->cs_gpio, 0);
    t = kzalloc(sizeof(struct spi_transfer), GFP_KERNEL);

    /*发送要读取的寄存器地址*/
    txdata[0] = reg|0x80;           /*读数据的时候寄存器地址的bit7位要置1*/
    t->tx_buf = txdata;             /*要发送的数据*/
    t->len = 1;
    spi_message_init(&msg);         /*初始化msg*/
    spi_message_add_tail(t, &msg);  /*将spi_transfer添加到spi_message*/
    ret = spi_sync(spi, &msg);           /*同步发送*/

    /*读取数据*/
    txdata[0] = 0xff;               /*随便一个值，此数据无意义*/
    t->rx_buf = buf;                /*读取到的数据*/
    t->len = len;                   /*要读取的数据长度*/
    spi_message_init(&msg);         /*初始化msg*/
    spi_message_add_tail(t, &msg);  /*将spi_transfer添加到spi_message*/
    ret = spi_sync(spi, &msg);           /*同步发送*/

    kfree(t);                       /*释放内存*/
    gpio_set_value(dev->cs_gpio, 1);/*片选拉高，释放ICM20608*/

    return ret;    
}

static unsigned char icm20608_readone(struct icm20608_dev *dev, u8 reg)
{
    u8 data = 0;
    icm20608_read_regs(dev, reg, &data, 1);
    return data;
}

static int icm20608_write_regs(struct icm20608_dev *dev, u8 reg, u8 *buf, u8 len)
{
    int ret;
    u8 txdata[len];
    struct spi_message msg;
    struct spi_transfer *t;
    struct spi_device *spi = (struct spi_device *)dev->private_data;

    /*片选拉低，选中ICM20608*/
    gpio_set_value(dev->cs_gpio, 0);
    t = kzalloc(sizeof(struct spi_transfer), GFP_KERNEL);

    /*发送要写的寄存器地址*/
    txdata[0] = reg & ~0x80;        /*写数据的时候寄存器地址的bit7位要清零*/
    t->tx_buf = txdata;             /*要发送的数据*/
    t->len = 1;
    spi_message_init(&msg);         /*初始化msg*/
    spi_message_add_tail(t, &msg);  /*将spi_transfer添加到spi_message*/
    ret = spi_sync(spi, &msg);           /*同步发送*/

    /*写数据*/
    t->tx_buf = buf;                /*要写入的数据*/
    t->len = len;                   /*要写入的数据长度*/
    spi_message_init(&msg);         /*初始化msg*/
    spi_message_add_tail(t, &msg);  /*将spi_transfer添加到spi_message*/
    ret = spi_sync(spi, &msg);           /*同步发送*/

    kfree(t);                       /*释放内存*/
    gpio_set_value(dev->cs_gpio, 1);/*片选拉高，释放ICM20608*/

    return ret;   
}

static void icm20608_writeone(struct icm20608_dev *dev, u8 reg, u8 buf)
{
    u8 data = buf;
    icm20608_write_regs(dev, reg, &data, 1);
}

static void icm20608_readdata(struct icm20608_dev *dev)
{
    unsigned char data[14];
    icm20608_read_regs(dev, ICM20_ACCEL_XOUT_H, data, 14);

    dev->accel_x_adc = (signed short)((data[0] << 8) | data[1]); 
	dev->accel_y_adc = (signed short)((data[2] << 8) | data[3]); 
	dev->accel_z_adc = (signed short)((data[4] << 8) | data[5]); 
	dev->temp_adc    = (signed short)((data[6] << 8) | data[7]); 
	dev->gyro_x_adc  = (signed short)((data[8] << 8) | data[9]); 
	dev->gyro_y_adc  = (signed short)((data[10] << 8) | data[11]);
	dev->gyro_z_adc  = (signed short)((data[12] << 8) | data[13]);
}
static void icm20608reg_init(void)
{
    u8 value = 0;

    icm20608_writeone(&icm20608dev, ICM20_PWR_MGMT_1, 0x80);		/* 复位，复位后为0x40,睡眠模式 */
	mdelay(50);
	icm20608_writeone(&icm20608dev, ICM20_PWR_MGMT_1, 0x01);		/* 关闭睡眠，自动选择时钟 */
	mdelay(50);

    value = icm20608_readone(&icm20608dev, 0x75);
    printk("ICM20608 ID= %#X\r\n", value);

    icm20608_writeone(&icm20608dev, ICM20_SMPLRT_DIV, 0x00); 	    /* 输出速率是内部采样率	*/
	icm20608_writeone(&icm20608dev, ICM20_GYRO_CONFIG, 0x18); 	    /* 陀螺仪±2000dps量程 */
	icm20608_writeone(&icm20608dev, ICM20_ACCEL_CONFIG, 0x18); 	    /* 加速度计±16G量程 */
	icm20608_writeone(&icm20608dev, ICM20_CONFIG, 0x04); 		    /* 陀螺仪低通滤波BW=20Hz */
	icm20608_writeone(&icm20608dev, ICM20_ACCEL_CONFIG2, 0x04); 	/* 加速度计低通滤波BW=21.2Hz */
	icm20608_writeone(&icm20608dev, ICM20_PWR_MGMT_2, 0x00); 	    /* 打开加速度计和陀螺仪所有轴 */
	icm20608_writeone(&icm20608dev, ICM20_LP_MODE_CFG, 0x00); 	    /* 关闭低功耗 */
	icm20608_writeone(&icm20608dev, ICM20_FIFO_EN, 0x00);		    /* 关闭FIFO	 */
}


static int icm20608_open (struct inode *inode, struct file *filp)
{
    printk("icm20608_open\r\n");
    filp->private_data = &icm20608dev;
    return 0;
}

static ssize_t icm20608_read (struct file *filp, char __user *buf, size_t cnt, loff_t *off_t)
{
    int err = 0;
    signed int data[7];
    struct icm20608_dev *dev = (struct icm20608_dev *)filp->private_data;

    icm20608_readdata(dev);

    data[0] = dev->gyro_x_adc;
    data[1] = dev->gyro_y_adc;
    data[2] = dev->gyro_z_adc;

    data[3] = dev->accel_x_adc;
    data[4] = dev->accel_y_adc;
    data[5] = dev->accel_z_adc;

    data[6] = dev->temp_adc;

    err = copy_to_user(buf, data, sizeof(data));
    // printk("icm20608_read\r\n");
    return 0;
}

static int icm20608_release (struct inode *inode, struct file *filp)
{
    printk("icm20608_release\r\n");
    return 0;
}
/*字符设备操作集*/
static struct file_operations icm20608_fops = {
    .owner	 = THIS_MODULE,
    .open	 = icm20608_open,
    .read	 = icm20608_read,
    .release = icm20608_release
};

static int icm20608_probe(struct spi_device *spi)
{
    int ret = 0;
    printk("icm20608_probe!\r\n");

    /*注册字符设备驱动*/
    icm20608dev.major = 0;   /*设置主设备号为0，确保让系统分配设备号*/
    /*1、创建设备号*/
    if(icm20608dev.major)         /*如果定义了设备号*/
    {
        icm20608dev.devid = MKDEV(icm20608dev.major, 0);
        ret = register_chrdev_region(icm20608dev.devid, icm20608_CNT, icm20608_NAME);
    }
    else                        /*如果没有定义设备号*/
    {
        ret = alloc_chrdev_region(&icm20608dev.devid, 0, icm20608_CNT, icm20608_NAME); /*申请设备号*/
        icm20608dev.major = MAJOR(icm20608dev.devid);
        icm20608dev.minor = MINOR(icm20608dev.devid);
    }
    if(ret < 0)
    {
        goto fail_devid;
    }
    printk("icm20608dev major=%d, minor=%d\r\n", icm20608dev.major, icm20608dev.minor);

    /*2初始话cdev*/
    icm20608dev.cdev.owner = THIS_MODULE;
    cdev_init(&icm20608dev.cdev, &icm20608_fops);

    /*3、添加一个cdev*/
    ret = cdev_add(&icm20608dev.cdev, icm20608dev.devid, icm20608_CNT);
    if(ret < 0)
    {
        goto fail_cdev;
    }

    /*4、创建类*/
    icm20608dev.class = class_create(THIS_MODULE, icm20608_NAME);
    if(IS_ERR(icm20608dev.class))
    {
        ret = PTR_ERR(icm20608dev.class);
        goto fail_class;
    }
    /*5、创建设备*/
    icm20608dev.device = device_create(icm20608dev.class, NULL, icm20608dev.devid, NULL, icm20608_NAME);
    if(IS_ERR(icm20608dev.device))
    {
        ret = PTR_ERR(icm20608dev.device);
        goto fail_device;
    }

    /*获取设备树中的片选信号*/
    icm20608dev.nd = of_find_node_by_path("/soc/aips-bus@02000000/spba-bus@02000000/ecspi@02010000");
    if(icm20608dev.nd == NULL){
        printk("ecsipi3 node not find!\r\n");
        return -EINVAL;
    }

    icm20608dev.cs_gpio = of_get_named_gpio(icm20608dev.nd, "cs-gpio", 0);
    if(icm20608dev.cs_gpio < 0){
        printk("can't get cs-gpio!\r\n");
        return -EINVAL;
    }else{
        printk("cs-gpio = %d\r\n",icm20608dev.cs_gpio);
    }

    /*设置cs输出为高电平*/
    ret = gpio_direction_output(icm20608dev.cs_gpio, 1);
    if(ret < 0){
        printk("can't set cs-gpio!\r\n");
    }

    /*初始化spi_device*/
    spi->mode = SPI_MODE_0;                 /*MODE0, CPOL=0, CPHA=0*/
    spi_setup(spi);
    icm20608dev.private_data = spi;         /*将数据保存在设备私有数据域中*/

    /*初始化ICM20608内部寄存器*/
    icm20608reg_init();

    printk("icm20608dev init()\r\n");
    return 0;

fail_device:
    class_destroy(icm20608dev.class);
fail_class:
    cdev_del(&icm20608dev.cdev);
fail_cdev:
    unregister_chrdev_region(icm20608dev.devid, icm20608_CNT);   
fail_devid:
    return ret;
}

static int icm20608_remove(struct spi_device *spi)
{
    /*注销设备驱动*/
    cdev_del(&icm20608dev.cdev);
    unregister_chrdev_region(icm20608dev.devid, icm20608_CNT);
    device_destroy(icm20608dev.class, icm20608dev.devid);
    class_destroy(icm20608dev.class);

    printk("icm20608_remove!\r\n");
    return 0;
}

/*传统匹配表*/
static const struct spi_device_id icm20608_id_table[] = {
	{ "alk,icm20608", 0 },
	{}
};

/*设备树匹配表*/
static const struct of_device_id icm20608_of_match[] = {
	{ .compatible = "alk,icm20608", },
	{ /* Sentinel */},
};

/*icm20608 driver结构体*/
static struct spi_driver icm20608_driver = {
	.probe = icm20608_probe,
	.remove = icm20608_remove,
    .driver = {
		.name = "icm20608",
		.owner = THIS_MODULE,
        .of_match_table = icm20608_of_match,
	},
	.id_table = icm20608_id_table,
};

static int __init icm20608_init(void)
{
    return spi_register_driver(&icm20608_driver);
}

static void __exit icm20608_exit(void)
{
    spi_unregister_driver(&icm20608_driver);
}


module_init(icm20608_init);
module_exit(icm20608_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("mankc");