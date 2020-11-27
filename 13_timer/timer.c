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
#include <linux/timer.h>

#define TIMER_CNT       1
#define TIMER_NAME      "timer"
#define LEDOFF          0
#define LEDON           1

#define CLOSE_CMD       _IO(0xEF, 1)
#define OPEN_CMD        _IO(0xEF, 2)       
#define SET_CMD         _IOW(0xEF, 3, int)

struct timer_dev{
    dev_t devid;
    int major;
    int minor;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
    struct timer_list timer;
    atomic_t timerperiod;
    int gpio_led;
};

struct timer_dev timer;


/*led初始化*/
static int led_init(struct timer_dev *dev){
    int ret = 0;

    /*获取设备节点*/
    dev->nd = of_find_node_by_path("/gpioled");
    if(dev->nd == NULL){
        ret = -EINVAL;
        goto fail_find_nd;
    }

    /*获取led的GPIO*/ 
    dev->gpio_led = of_get_named_gpio(dev->nd, "led-gpios", 0);
    if(dev->gpio_led < 0){
        ret = -EINVAL;
        goto fail_get_gpio;
    }
    printk("led gpio num = %d\r\n",timer.gpio_led);

    /*向内核申请io*/ 
    ret = gpio_request(dev->gpio_led, "led-gpio");
    if(ret){
        ret = -EBUSY;
        goto fail_reqs_gpio;
    }
    
    /*设置led的GPIO为输出*/
    ret = gpio_direction_output(dev->gpio_led, 1);  /*设置gpio输出为high,默认关闭led*/ 
    if(ret){
        ret = -EINVAL;
        goto fail_gpio_set;
    }

    printk("LED gpio init success!\r\n");
    return 0;

fail_gpio_set:
    gpio_free(dev->gpio_led);    
fail_reqs_gpio:
    printk("Failed to request the led gpio!\r\n");
fail_get_gpio:
    printk("can't find led gpio\r\n");
fail_find_nd:
    return ret;

}

/*
* @description     :打开设备
* @param - inode   :传递给驱动的inode
* @param - filp    :设备文件，file结构体有个叫做private_data的成员变量
*                   一般在open的时候将private_data指向设备结构体。
* @return          : 0 成功；
*/
static int timer_open (struct inode *inode, struct file *filp)
{
    filp->private_data = &timer;
    return 0;
}

/*
* @description : 关闭/释放设备
* @param - filp : 要关闭的设备文件(文件描述符)
* @return : 0 成功;其他 失败
*/
static int timer_release (struct inode *inode, struct file *filp)
{
    return 0;
}

static long timer_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    int value = 0;
    struct timer_dev *dev = filp->private_data;
    switch(cmd) {
        case CLOSE_CMD:
            del_timer_sync(&dev->timer); 
            break;
        case OPEN_CMD:
            mod_timer(&dev->timer, jiffies + msecs_to_jiffies(atomic_read(&dev->timerperiod)));
            break;
        case SET_CMD:
            ret = copy_from_user(&value, (int *)arg, sizeof(int));
            if(ret < 0){
                return -EINVAL;
            }
            atomic_set(&dev->timerperiod, value);
            mod_timer(&dev->timer, jiffies + msecs_to_jiffies(atomic_read(&dev->timerperiod)));
            break;
    }

    return ret;

}

static struct file_operations timer_fops = {
    .owner = THIS_MODULE,
    .open = timer_open,
    .release = timer_release,
    .unlocked_ioctl = timer_ioctl,
};

/*定时器回调函数*/ 
static void timer_func(unsigned long arg){
    struct timer_dev *dev = (struct timer_dev *)arg;
    static int status = 1;

    status = !status;
    gpio_set_value(dev->gpio_led, status);
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(atomic_read(&dev->timerperiod)));
}

static int __init timer_init(void)
{
    int ret = 0;

    /*申请设备号*/ 
    timer.major = 0;
    if(timer.major){
        timer.devid = MKDEV(timer.major, 0);
        ret = register_chrdev_region(timer.devid, TIMER_CNT, TIMER_NAME);
    }else{
        ret = alloc_chrdev_region(&timer.devid, 0, TIMER_CNT, TIMER_NAME);
        timer.major = MAJOR(timer.devid);
        timer.minor = MINOR(timer.devid);
    }
    if(ret < 0){
        goto fail_devid;
    }
    printk("gpiotimer major = %d, minor = %d\r\n", timer.major, timer.minor);

    /*注册字符设备*/ 
    timer.cdev.owner = THIS_MODULE;
    cdev_init(&timer.cdev, &timer_fops);
    ret = cdev_add(&timer.cdev, timer.devid, TIMER_CNT);
    if(ret < 0){
        goto fail_cdev;
    }

    /*自动创建设备节点*/ 
    timer.class = class_create(THIS_MODULE, TIMER_NAME);
    if(IS_ERR(timer.class)){
        ret = PTR_ERR(timer.class);
        goto fail_class;
    }

    timer.device = device_create(timer.class, NULL, timer.devid, NULL, TIMER_NAME);
    if(IS_ERR(timer.device)){
        ret = PTR_ERR(timer.device);
        goto fail_device;
    }

    /*初始化led*/ 
    ret = led_init(&timer);
    if(ret < 0) {
        goto fail_led_init;
    }

    /* 初始化定时器 */
    init_timer(&timer.timer); 
    atomic_set(&timer.timerperiod,500);
    timer.timer.function = timer_func;
    timer.timer.expires = jiffies + msecs_to_jiffies(atomic_read(&timer.timerperiod));
    timer.timer.data = (unsigned long)&timer;
    add_timer(&timer.timer);

    printk("timer_init()\r\n");
    return 0;

fail_led_init:
    device_destroy(timer.class, timer.devid); 
fail_device:
    class_destroy(timer.class); 
fail_class:
    cdev_del(&timer.cdev); 
fail_cdev:
    unregister_chrdev_region(timer.devid, TIMER_CNT);
fail_devid:
    return ret;

}

static void __exit timer_exit(void)
{
    /*关闭LED*/ 
    gpio_set_value(timer.gpio_led, 1); 
    /*释放ledgpio*/ 
    gpio_free(timer.gpio_led);
    /*删除定时器*/
    del_timer(&timer.timer); 
    /*摧毁设备*/
    device_destroy(timer.class, timer.devid); 
    /*摧毁类*/
    class_destroy(timer.class); 
    /*删除字符设备*/
    cdev_del(&timer.cdev); 
    /*释放设备号*/ 
    unregister_chrdev_region(timer.devid, TIMER_CNT);

    printk("timer_exit()\r\n");
}


module_init(timer_init);
module_exit(timer_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("mankc");