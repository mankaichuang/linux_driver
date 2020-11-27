#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>

#define MISCBEEP_MINOR      144
#define MISCBEEP_NAME       "miscbeep"
#define BEEPOFF             0
#define BEEPON              1

struct miscbeep_dev {
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *beepnd;
    int gpio_beep;
};

struct miscbeep_dev miscbeep;

static int miscbeep_open (struct inode *inode, struct file *filp){
    filp->private_data = &miscbeep;
    return 0;
}

static ssize_t miscbeep_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt){
    int ret = 0;
    u8 databuf[1];
    u8 beepstate;
    struct miscbeep_dev *beepdev = filp->private_data;

    ret = copy_from_user(databuf,buf,cnt);
    if(ret < 0){
        printk("write buffer error!\r\n");
        return ret;
    }

    beepstate = databuf[0];
    if(beepstate == BEEPON){
        gpio_set_value(beepdev->gpio_beep, 0);
    }else if(beepstate == BEEPOFF){
        gpio_set_value(beepdev->gpio_beep, 1);
    }

    return cnt;
}

static int miscbeep_release (struct inode *inode, struct file *filp){
    return 0;
}

static struct file_operations miscbeep_fops = {
    .owner = THIS_MODULE,
    .open  = miscbeep_open,
    .write = miscbeep_write,
    .release = miscbeep_release
};

static struct miscdevice beep_miscdev = {
    .minor = MISCBEEP_MINOR,
    .name = MISCBEEP_NAME,
    .fops = &miscbeep_fops,
};
/*
* @description : platform 驱动的 probe 函数，当驱动与设备
* 匹配以后此函数就会执行
* @param - dev : platform 设备
* @return : 0，成功;其他负值,失败
*/
static int beep_probe(struct platform_device *dev)
{
    int ret = 0;
    printk("miscbeep probe!\r\n");
    
    /*1、获取蜂鸣器节点*/
#if 0 
    miscbeep.beepnd = of_find_node_by_path("/beep");
    if(miscbeep.beepnd == NULL){
        return -EINVAL;
    }
#endif
    miscbeep.beepnd = dev->dev.of_node;

    /*2、获取beep的GPIO号*/
    miscbeep.gpio_beep = of_get_named_gpio(miscbeep.beepnd,"beep-gpios", 0);
    if(miscbeep.gpio_beep < 0){
        printk("can't find the beep gpio\r\n");
        return -EINVAL;
    }

    /*3、向系统请求gpio*/
    ret = gpio_request(miscbeep.gpio_beep, "BEEP"); 
    if(ret){
        printk("Failed to request beep gpio\r\n");
        return ret;
    }

    /*4、配置IO的输出,默认输出为高电平，关闭蜂鸣器*/ 
    ret = gpio_direction_output(miscbeep.gpio_beep, 1);
    if(ret) {
        return ret;
    }

    /*5、注册misc设备驱动*/
    ret = misc_register(&beep_miscdev);
    if(ret){
        printk("misc device register failed\r\n");
        return ret;
    }

    printk("beep_init()\r\n");
    return 0;
}

static int beep_remove(struct platform_device *dev)
{
    /*注销蜂鸣器时关闭蜂鸣器*/
    gpio_set_value(miscbeep.gpio_beep, 1);
    gpio_free(miscbeep.gpio_beep);
    /*注销MISC设备驱动*/
    misc_deregister(&beep_miscdev);

    printk("miscbeep remove!\r\n");
    return 0;
}

/*匹配列表*/
static struct of_device_id	beep_of_match_table[] = {
    {.compatible = "ALK,beep"},
	{ /* sentinel */ }
};

/*构建platform驱动结构体*/
static struct platform_driver miscbeep_driver = {
    .driver	= {
        .name	= "imx6ul-beep",
        .of_match_table = beep_of_match_table,
    },
    .probe	= beep_probe,
    .remove	= beep_remove,
};

/*
* @description : 驱动入口函数
* @param : 无
* @return : 0 成功;
*/
static int __init miscbeep_init(void)
{   
    return platform_driver_register(&miscbeep_driver);
}

/*
* @description : 驱动出口函数
* @param : 无
*/
static void __exit miscbeep_exit(void)
{
    platform_driver_unregister(&miscbeep_driver);
}

module_init(miscbeep_init);
module_exit(miscbeep_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("mankc");