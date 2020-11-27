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
#include <linux/of_address.h>

#define  LED_CNT     1
#define  LED_NAME    "dtsled"
#define  LEDOFF      0
#define  LEDON       1

/*led设备结构体*/
struct led_dev{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *led_nd;         /*设备节点*/
}; 

struct led_dev led;

/*寄存器映射的虚拟地址指针*/
static void __iomem *IMX6U_CCM_CCGR1; 
static void __iomem *SW_MUX_GPIO1_IO03;
static void __iomem *SW_PAD_GPIO1_IO03;
static void __iomem *GPIO1_DR;
static void __iomem *GPIO1_GDIR;

/*
* @description : LED 打开/关闭
* @param - sta : LEDON(0) 打开 LED， LEDOFF(1) 关闭 LED
* @return : 无
*/
void led_switch(uint8_t ledstate)
{
    uint32_t val = 0;
    if(ledstate == LEDON)
    {
        val = readl(GPIO1_DR);
        val &= ~(1 << 3);
        writel(val,GPIO1_DR);
    }
    else if (ledstate == LEDOFF)
    {
        val = readl(GPIO1_DR);
        val |= (1 << 3);
        writel(val,GPIO1_DR);
    }
}

/*
* @description     :打开设备
* @param - inode   :传递给驱动的inode
* @param - filp    :设备文件，file结构体有个叫做private_data的成员变量
*                   一般在open的时候将private_data指向设备结构体。
* @return          : 0 成功；
*/
static int led_open(struct inode *inode, struct file *flip)
{
    flip->private_data = &led;
    return 0;
}

/*
* @description : 向设备写数据
* @param - filp : 设备文件，表示打开的文件描述符
* @param - buf : 要给设备写入的数据
* @param - cnt : 要写入的数据长度
* @param - offt : 相对于文件首地址的偏移
* @return : 写入的字节数，如果为负值，表示写入失败
*/
static ssize_t led_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *off_t)
{
    int retvalue;
    uint8_t databuf[1];
    uint8_t ledstate;

    retvalue = copy_from_user(databuf, buf, cnt);
    if(retvalue < 0)
    {
        printk("kernel write failed!\r\n");
        return -EFAULT;
    }

    ledstate = databuf[0];
    led_switch(ledstate);
    
    return 0;
}
/*
* @description : 关闭/释放设备
* @param - filp : 要关闭的设备文件(文件描述符)
* @return : 0 成功;其他 失败
*/
static int led_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static struct file_operations dtsled_fops = {
    .owner	 = THIS_MODULE,
    .open	 = led_open,
    .write	 = led_write,
    .release = led_release
};

static int __init led_init(void)
{
    u8 i=0;
    uint32_t val = 0;
    int ret = 0;
    const char *led_info;
    u32 regdata[10];
    /*注册字符设备*/
    /*1、申请设备号*/  
    led.major = 0;
    if(led.major)
    {
        led.devid = MKDEV(led.major, 0);
        ret = register_chrdev_region(led.devid, LED_CNT, LED_NAME);
    }
    else
    {
        ret = alloc_chrdev_region(&led.devid, 0, LED_CNT, LED_NAME);
        led.major = MAJOR(led.devid);
        led.minor = MINOR(led.devid);
    }
    if(ret < 0)
    {
        goto fail_devid;
    }
    
    /*2、添加字符设备*/ 
    led.cdev.owner = THIS_MODULE;
    cdev_init(&led.cdev, &dtsled_fops);
    ret = cdev_add(&led.cdev, led.devid, LED_CNT);
    if(ret < 0)
    {
        goto fail_cdev;
    }

    /*3、自动创建设备节点*/
    led.class = class_create(THIS_MODULE, LED_NAME);
    if(IS_ERR(led.class))
    {
        ret = PTR_ERR(led.class);
        goto fail_class;
    }
    led.device = device_create(led.class, NULL, led.devid, NULL, LED_NAME);
    if(IS_ERR(led.device))
    {
        ret = PTR_ERR(led.device);
        goto fail_device;
    }

    /*获取设备树属性信息*/ 
    led.led_nd = of_find_node_by_path("/alkled");
    if(led.led_nd == NULL)
    {
        ret = -EINVAL;
        goto fail_find_nd;
    }
    /*获取设备属性信息*/ 
    ret = of_property_read_string(led.led_nd, "compatible", &led_info);
    if(ret < 0)
    {
        goto fail_get_info;
    }
    else
    {
        printk("compatible = %s\r\n", led_info);
    }

    ret = of_property_read_string(led.led_nd, "status", &led_info);
    if(ret < 0)
    {
        goto fail_get_info;
    }
    else
    {
        printk("status = %s\r\n", led_info);
    }

    /*获取寄存器地址信息*/ 
    ret = of_property_read_u32_array(led.led_nd, "reg", regdata, 10);
    if(ret < 0)
    {
        goto fail_get_info;
    }
    else
    {
        printk("regdata =[ ");
        for(i=0; i<10; i++)
        {
            printk("%#X ",regdata[i]);
        }
        printk("]\r\n");
    }

    /*初始化LED*/
    /*1、寄存器地址映射*/
    // IMX6U_CCM_CCGR1     = ioremap(regdata[0], regdata[1]);
    // SW_MUX_GPIO1_IO03   = ioremap(regdata[2], regdata[3]);
    // SW_PAD_GPIO1_IO03   = ioremap(regdata[4], regdata[5]);
    // GPIO1_DR            = ioremap(regdata[6], regdata[7]);
    // GPIO1_GDIR          = ioremap(regdata[8], regdata[9]);

    IMX6U_CCM_CCGR1 = of_iomap(led.led_nd, 0);
    SW_MUX_GPIO1_IO03 = of_iomap(led.led_nd, 1);
    SW_PAD_GPIO1_IO03 = of_iomap(led.led_nd, 2);
    GPIO1_DR = of_iomap(led.led_nd, 3);
    GPIO1_GDIR = of_iomap(led.led_nd, 4);

    /*2、使能GPIO1时钟*/
    val = readl(IMX6U_CCM_CCGR1);
    val &= ~(3 << 26);   /*清除设置位*/
    val |= (3 << 26);    /*设置*/
    writel(val, IMX6U_CCM_CCGR1);

    /*3、设置GPIO1_IO3的复用功能，将其复用为GPIO1_IO3，最后设置IO属性*/
    writel(5, SW_MUX_GPIO1_IO03);
    writel(0x10b0, SW_PAD_GPIO1_IO03);

    /*4、设置gpio为输出*/
    val = readl(GPIO1_GDIR);  
    val &= ~(1 << 3);
    val |= (1 <<3);
    writel(val,GPIO1_GDIR);

    /*5、默认关闭led*/
    val = readl(GPIO1_DR);
    val |= (1 << 3);
    writel(val, GPIO1_DR);

    printk("dtsled init()\r\n");
    return 0;

fail_get_info:
fail_find_nd:
    device_destroy(led.class, led.devid);
fail_device:
    class_destroy(led.class);
fail_class:
    cdev_del(&led.cdev);
fail_cdev:
    unregister_chrdev_region(led.devid, LED_CNT);
fail_devid:
    return ret;
}

static void __exit led_exit(void)
{
    u32 val = 0;
    /*关灯*/ 
    val = readl(GPIO1_DR);
    val |= (1 << 3);
    writel(val, GPIO1_DR);
    /*取消映射*/
    iounmap(IMX6U_CCM_CCGR1);
    iounmap(SW_MUX_GPIO1_IO03);
    iounmap(SW_PAD_GPIO1_IO03);
    iounmap(GPIO1_DR);
    iounmap(GPIO1_GDIR);
    /*删除字符设备*/ 
    cdev_del(&led.cdev);
    /*释放设备号*/ 
    unregister_chrdev_region(led.devid, LED_CNT);
    /*摧毁设备*/ 
    device_destroy(led.class, led.devid);
    /*摧毁类*/ 
    class_destroy(led.class);

    printk("dtsled exit!\r\n");
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("mankc");