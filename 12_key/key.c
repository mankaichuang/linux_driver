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

#define KEY_CNT     1
#define KEY_NAME    "key"
#define KEY0_VALUE  0XF0
#define INVAKEY     0X00

struct key_dev{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
    int major;
    int minor;

    int gpio_key;

    atomic_t keyvalue;
};

struct key_dev key;


/*
* @description : keyio初始化
* @return : 0 初始化成功，<0 初始化失败
*/
static int keyio_init(void)
{
    int ret = 0;

    /*获取设备节点*/
    key.nd = of_find_node_by_path("/key");
    if(key.nd == NULL){
        return -EINVAL;
    }

    /*获取led的GPIO*/ 
    key.gpio_key = of_get_named_gpio(key.nd, "key-gpios", 0);
    if(key.gpio_key < 0){
        printk("can't get key gpio\r\n");
        return -EINVAL;
    }
    printk("key gpio num = %d\r\n",key.gpio_key);

    /*向内核申请io*/ 
    ret = gpio_request(key.gpio_key, "key-gpio");
    if(ret){
        printk("Failed to request the key gpio\r\n");
        return -EINVAL;
    }

    /*设置key的GPIO为输入*/
    ret = gpio_direction_input(key.gpio_key);  /*设置key为输入*/ 
    if(ret){
        return -EINVAL;
    }

    return 0;
}

/*
* @description     :打开设备
* @param - inode   :传递给驱动的inode
* @param - filp    :设备文件，file结构体有个叫做private_data的成员变量
*                   一般在open的时候将private_data指向设备结构体。
* @return          : 0 成功；
*/
static int key_open (struct inode *inode, struct file *filp)
{   
    int ret = 0;
    filp->private_data = &key;

    ret = keyio_init();
    if(ret < 0){
        printk("keyio_init is failed!\r\n");
        return ret;
    }
    return 0;
}

/*
* @description : 从设备读取数据
* @param - filp : 设备文件，表示打开的文件描述符
* @param - buf : 要给设备获取的数据
* @param - cnt : 要读取的数据长度
* @param - offt : 相对于文件首地址的偏移
* @return : 读入的字节数，如果为负值，表示读取失败
*/
static ssize_t key_read (struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{ 
    int ret = 0;
    u8 value;
    struct key_dev *dev = filp->private_data;

    if(gpio_get_value(dev->gpio_key) == 0){
        while(!gpio_get_value(dev->gpio_key));
        atomic_set(&dev->keyvalue, KEY0_VALUE);
    }else{
        atomic_set(&dev->keyvalue, INVAKEY);
    }

    value = atomic_read(&dev->keyvalue);
    ret = copy_to_user(buf, &value, sizeof(value));

    return ret;
}
/*
* @description : 关闭/释放设备
* @param - filp : 要关闭的设备文件(文件描述符)
* @return : 0 成功;其他 失败
*/
static int key_release (struct inode *inode, struct file *filp)
{
    struct key_dev *dev = filp->private_data;

    /*释放key的GPIO*/ 
    gpio_free(dev->gpio_key);
    return 0;
}

static struct file_operations key_fops = {
    .owner = THIS_MODULE,
    .open = key_open,
    .read = key_read,
    .release = key_release,
};

static int __init mykey_init(void)
{
    int ret = 0;

    /*初始化按键值*/ 
    atomic_set(&key.keyvalue, INVAKEY);

    /*申请设备号*/ 
    key.major = 0;
    if(key.major){
        key.devid = MKDEV(key.major, 0);
        ret = register_chrdev_region(key.devid, KEY_CNT, KEY_NAME);
    }else{
        ret = alloc_chrdev_region(&key.devid, 0, KEY_CNT, KEY_NAME);
        key.major = MAJOR(key.devid);
        key.minor = MINOR(key.devid);
    }
    if(ret < 0){
        goto fail_devid;
    }
    printk("key major = %d, minor = %d\r\n", key.major, key.minor);

    /*注册字符设备*/ 
    key.cdev.owner = THIS_MODULE;
    cdev_init(&key.cdev, &key_fops);
    ret = cdev_add(&key.cdev, key.devid, KEY_CNT);
    if(ret < 0){
        goto fail_cdev;
    }

    /*自动创建设备节点*/ 
    key.class = class_create(THIS_MODULE, KEY_NAME);
    if(IS_ERR(key.class)){
        ret = PTR_ERR(key.class);
        goto fail_class;
    }

    key.device = device_create(key.class, NULL, key.devid, NULL, KEY_NAME);
    if(IS_ERR(key.device)){
        ret = PTR_ERR(key.device);
        goto fail_device;
    }
    printk("key_init()\r\n");
    return 0;

fail_device:
    class_destroy(key.class); 
fail_class:
    cdev_del(&key.cdev); 
fail_cdev:
    unregister_chrdev_region(key.devid, KEY_CNT);
fail_devid:
    return ret;

}

static void __exit mykey_exit(void)
{
    /*摧毁设备*/
    device_destroy(key.class, key.devid); 
    /*摧毁类*/
    class_destroy(key.class); 
    /*删除字符设备*/
    cdev_del(&key.cdev); 
    /*释放设备号*/ 
    unregister_chrdev_region(key.devid, KEY_CNT);

    printk("key_exit()\r\n");
}


module_init(mykey_init);
module_exit(mykey_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("mankc");