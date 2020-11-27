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

#define LED_CNT     1
#define LED_NAME    "gpioled"
#define LEDOFF      0
#define LEDON       1

struct led_dev{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *lednd;
    int gpio_led;
    int major;
    int minor;

    struct semaphore sem;
};

struct led_dev led;


/*
* @description : LED 打开/关闭
* @param - sta : LEDON(0) 打开 LED， LEDOFF(1) 关闭 LED
* @return : 无
*/
void led_switch(uint8_t ledstate)
{
    if(ledstate == LEDON)
    {
        gpio_set_value(led.gpio_led, 0); 
    }
    else if (ledstate == LEDOFF)
    {
        gpio_set_value(led.gpio_led, 1); 
    }
}

/*
* @description     :打开设备
* @param - inode   :传递给驱动的inode
* @param - filp    :设备文件，file结构体有个叫做private_data的成员变量
*                   一般在open的时候将private_data指向设备结构体。
* @return          : 0 成功；
*/
static int led_open (struct inode *inode, struct file *filp)
{
    filp->private_data = &led;

    down(&led.sem);
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
static ssize_t led_write (struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
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
    
    return cnt;
}
/*
* @description : 关闭/释放设备
* @param - filp : 要关闭的设备文件(文件描述符)
* @return : 0 成功;其他 失败
*/
static int led_release (struct inode *inode, struct file *filp)
{
    struct led_dev *dev = filp->private_data;
    up(&dev->sem);
    return 0;
}

static struct file_operations gpioled_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .write = led_write,
    .release = led_release,
};

static int __init led_init(void)
{
    int ret = 0;

    /*初始化信号量*/ 
    sema_init(&led.sem, 1);

    /*申请设备号*/ 
    led.major = 0;
    if(led.major){
        led.devid = MKDEV(led.major, 0);
        ret = register_chrdev_region(led.devid, LED_CNT, LED_NAME);
    }else{
        ret = alloc_chrdev_region(&led.devid, 0, LED_CNT, LED_NAME);
        led.major = MAJOR(led.devid);
        led.minor = MINOR(led.devid);
    }
    if(ret < 0){
        goto fail_devid;
    }
    printk("gpioled major = %d, minor = %d\r\n", led.major, led.minor);

    /*注册字符设备*/ 
    led.cdev.owner = THIS_MODULE;
    cdev_init(&led.cdev, &gpioled_fops);
    ret = cdev_add(&led.cdev, led.devid, LED_CNT);
    if(ret < 0){
        goto fail_cdev;
    }

    /*自动创建设备节点*/ 
    led.class = class_create(THIS_MODULE, LED_NAME);
    if(IS_ERR(led.class)){
        ret = PTR_ERR(led.class);
        goto fail_class;
    }

    led.device = device_create(led.class, NULL, led.devid, NULL, LED_NAME);
    if(IS_ERR(led.device)){
        ret = PTR_ERR(led.device);
        goto fail_device;
    }

    /*获取设备节点*/
    led.lednd = of_find_node_by_path("/gpioled");
    if(led.lednd == NULL){
        ret = -EINVAL;
        goto fail_find_nd;
    }

    /*获取led的GPIO*/ 
    led.gpio_led = of_get_named_gpio(led.lednd, "led-gpios", 0);
    if(led.gpio_led < 0){
        printk("can't find led gpio\r\n");
        ret = -EINVAL;
        goto fail_get_gpio;
    }
    printk("led gpio num = %d\r\n",led.gpio_led);

    /*向内核申请io*/ 
    ret = gpio_request(led.gpio_led, "led-gpio");
    if(ret){
        printk("Failed to request the led gpio\r\n");
        goto fail_get_gpio;
    }

    /*设置LED的GPIO为输出*/
    ret = gpio_direction_output(led.gpio_led, 1);  /*设置gpio输出为high,默认关闭LED*/ 
    if(ret){
        goto fail_gpio_set;
    }

    // /*打开LED*/
    // gpio_set_value(led.gpio_led, 0); 

    printk("led_init()\r\n");
    return 0;

fail_gpio_set:
    gpio_free(led.gpio_led);
fail_get_gpio:
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
    /*关闭LED*/ 
    gpio_set_value(led.gpio_led, 1); 
    /*释放ledgpio*/ 
    gpio_free(led.gpio_led);
    /*摧毁设备*/
    device_destroy(led.class, led.devid); 
    /*摧毁类*/
    class_destroy(led.class); 
    /*删除字符设备*/
    cdev_del(&led.cdev); 
    /*释放设备号*/ 
    unregister_chrdev_region(led.devid, LED_CNT);

    printk("led_exit()\r\n");
}


module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("mankc");