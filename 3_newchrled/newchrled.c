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

#define   NEWCHRLED_CNT     1               /*设备号个数*/
#define   NEWCHRLED_NAME    "newchrled"     /*设备名称*/
#define   LEDOFF            0
#define   LEDON             1


/*寄存器物理地址*/ 
#define   CCM_CCGR1_BASE            (0X020C406C)
#define   SW_MUX_GPIO1_IO03_BASE    (0X020E0068)
#define   SW_PAD_GPIO1_IO03_BASE    (0X020E02F4)
#define   GPIO1_DR_BASE             (0X0209C000)
#define   GPIO1_GDIR_BASE           (0X0209C004)

/*寄存器映射的虚拟地址指针*/
static void __iomem *IMX6U_CCM_CCGR1; 
static void __iomem *SW_MUX_GPIO1_IO03;
static void __iomem *SW_PAD_GPIO1_IO03;
static void __iomem *GPIO1_DR;
static void __iomem *GPIO1_GDIR;

/*newchrled设备结构体*/
struct newchrled_dev{
    dev_t   devid;              /*设备号*/
    struct cdev cdev;           /*cdev*/
    struct class *class;        /*类*/
    struct device *device;      /*设备*/
    int major;                  /*主设备号*/
    int minor;                  /*次设备号*/
}; 

struct newchrled_dev newchrled;

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
    flip->private_data = &newchrled;
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


/*字符设备操作函数结构体定义*/
static struct file_operations newchrled_fops = {
    .owner	 = THIS_MODULE,
    .open	 = led_open,
    .write	 = led_write,
    .release = led_release
};

/*
* @description : 驱动入口函数
* @param : 无
* @return : 0 成功;
*/
static int __init led_init(void)
{
    uint32_t val = 0;
    int ret;
    /*初始化LED*/
    /*1、寄存器地址映射*/
    IMX6U_CCM_CCGR1     = ioremap(CCM_CCGR1_BASE, 4);
    SW_MUX_GPIO1_IO03   = ioremap(SW_MUX_GPIO1_IO03_BASE, 4);
    SW_PAD_GPIO1_IO03   = ioremap(SW_PAD_GPIO1_IO03_BASE, 4);
    GPIO1_DR            = ioremap(GPIO1_DR_BASE, 4);
    GPIO1_GDIR          = ioremap(GPIO1_GDIR_BASE, 4);

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

    /*注册字符设备驱动*/
    newchrled.major = 0;   /*设置主设备号为0，确保让系统分配设备号*/
    /*1、创建设备号*/
    if(newchrled.major)         /*如果定义了设备号*/
    {
        newchrled.devid = MKDEV(newchrled.major, 0);
        ret = register_chrdev_region(newchrled.devid, NEWCHRLED_CNT, NEWCHRLED_NAME);
    }
    else                        /*如果没有定义设备号*/
    {
        ret = alloc_chrdev_region(&newchrled.devid, 0, NEWCHRLED_CNT, NEWCHRLED_NAME); /*申请设备号*/
        newchrled.major = MAJOR(newchrled.devid);
        newchrled.minor = MINOR(newchrled.devid);
    }
    if(ret < 0)
    {
        goto fail_devid;
    }
    printk("newchrlde major=%d, minor=%d\r\n", newchrled.major, newchrled.minor);

    /*2初始话cdev*/
    newchrled.cdev.owner = THIS_MODULE;
    cdev_init(&newchrled.cdev, &newchrled_fops);

    /*3、添加一个cdev*/
    ret = cdev_add(&newchrled.cdev, newchrled.devid, NEWCHRLED_CNT);
    if(ret < 0)
    {
        goto fail_cdev;
    }

    /*4、创建类*/
    newchrled.class = class_create(THIS_MODULE, NEWCHRLED_NAME);
    if(IS_ERR(newchrled.class))
    {
        ret = PTR_ERR(newchrled.class);
        goto fail_class;
    }
    /*5、创建设备*/
    newchrled.device = device_create(newchrled.class, NULL, newchrled.devid, NULL, NEWCHRLED_NAME);
    if(IS_ERR(newchrled.device))
    {
        ret = PTR_ERR(newchrled.device);
        goto fail_device;
    }

    printk("newchrled init()\r\n");
    return 0;

fail_device:
    class_destroy(newchrled.class);
fail_class:
    cdev_del(&newchrled.cdev);
fail_cdev:
    unregister_chrdev_region(newchrled.devid, NEWCHRLED_CNT);   
fail_devid:
    return ret;
}

/*
* @description : 驱动出口函数
* @param : 无
*/
static void __exit led_exit(void)
{
    /*取消映射*/
    iounmap(IMX6U_CCM_CCGR1);
    iounmap(SW_MUX_GPIO1_IO03);
    iounmap(SW_PAD_GPIO1_IO03);
    iounmap(GPIO1_DR);
    iounmap(GPIO1_GDIR);

    /*注销设备驱动*/
    cdev_del(&newchrled.cdev);
    unregister_chrdev_region(newchrled.devid, NEWCHRLED_CNT);
    device_destroy(newchrled.class, newchrled.devid);
    class_destroy(newchrled.class);

    printk("newchrled exit!\r\n");
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("mankc");