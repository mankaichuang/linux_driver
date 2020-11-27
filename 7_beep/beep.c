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

#define BEEP_CNT     1
#define BEEP_NAME    "beep"
#define BEEPOFF      0
#define BEEPON       1

struct beep_dev {
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *beepnd;
    int gpio_beep;
    int major;
    int minor;
};

struct beep_dev beep;

static int beep_open (struct inode *inode, struct file *filp){
    filp->private_data = &beep;
    return 0;
}

static ssize_t beep_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt){
    int ret = 0;
    u8 databuf[1];
    u8 beepstate;
    struct beep_dev *beepdev = filp->private_data;

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

static int beep_release (struct inode *inode, struct file *filp){
    return 0;
}
static struct file_operations beep_fops = {
    .owner = THIS_MODULE,
    .open  = beep_open,
    .write = beep_write,
    .release = beep_release
};

static int __init beep_init(void){
    int ret = 0;

    /*1、申请设备号*/
    beep.major = 0;
    if(beep.major){
        beep.devid = MKDEV(beep.major, 0);
        ret = register_chrdev_region(beep.devid, BEEP_CNT, BEEP_NAME);
    }else{
        ret = alloc_chrdev_region(&beep.devid, 0, BEEP_CNT, BEEP_NAME);
        beep.major = MAJOR(beep.devid);
        beep.minor = MINOR(beep.devid);
    } 
    if(ret < 0){
        goto fail_devid;
    }
    printk("beep major = %d, minor = %d\r\n", beep.major,beep.minor);

    /*2、注册字符设备*/ 
    beep.cdev.owner = THIS_MODULE;
    cdev_init(&beep.cdev, &beep_fops);
    ret = cdev_add(&beep.cdev, beep.devid, BEEP_CNT);
    if(ret < 0){
        goto fail_cdev;
    }

    /*3、自动创建设备节点*/
    beep.class = class_create(THIS_MODULE, BEEP_NAME); 
    if(IS_ERR(beep.class)){
        ret = PTR_ERR(beep.class);
        goto fail_class;
    }

    beep.device = device_create(beep.class, NULL, beep.devid, NULL, BEEP_NAME);
    if(IS_ERR(beep.device)){
        ret = PTR_ERR(beep.device);
        goto fail_device;
    }

    /*4、获取蜂鸣器节点*/ 
    beep.beepnd = of_find_node_by_path("/beep");
    if(beep.beepnd == NULL){
        ret = -EINVAL;
        goto fail_node;
    }

    /*5、获取beep的GPIO号*/
    beep.gpio_beep = of_get_named_gpio(beep.beepnd,"beep-gpios", 0);
    if(beep.gpio_beep < 0){
        printk("can't find the beep gpio\r\n");
        ret = -EINVAL;
        goto fail_node;
    }

    /*6、向系统请求gpio*/
    ret = gpio_request(beep.gpio_beep, "BEEP"); 
    if(ret){
        printk("Failed to request beep gpio\r\n");
        goto fail_gpio_rq;
    }

    /*7、配置IO的输出,默认输出为高电平，关闭蜂鸣器*/ 
    ret = gpio_direction_output(beep.gpio_beep, 1);
    if(ret) {
        goto fail_gpio_rq;
    }

    printk("beep_init()\r\n");
    return 0;

fail_gpio_rq:
    gpio_free(beep.gpio_beep); 
fail_node:
    device_destroy(beep.class, beep.devid); 
fail_device:
    class_destroy(beep.class);
fail_class:
    cdev_del(&beep.cdev);    
fail_cdev:
    unregister_chrdev_region(beep.devid, BEEP_CNT);
fail_devid:
    return ret;
}

static void __exit beep_exit(void){
    /*关闭蜂鸣器*/
    gpio_set_value(beep.gpio_beep, 1); 
    /*释放gpio*/
    gpio_free(beep.gpio_beep); 
    /*摧毁设备*/
    device_destroy(beep.class, beep.devid); 
    /*摧毁类*/ 
    class_destroy(beep.class);
    /*删除设备*/
    cdev_del(&beep.cdev);
    /*释放设备号*/ 
    unregister_chrdev_region(beep.devid, BEEP_CNT);
    printk("beep_exit()\r\n");
}

module_init(beep_init);
module_exit(beep_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("mankc");