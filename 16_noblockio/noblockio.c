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
#include <linux/poll.h>

#define KEY_CNT     1
#define KEY_NAME    "noblockio"
#define KEY_NUM     1
#define KEY0_VALUE  0X01
#define INVAKEY     0XFF

struct irq_keydesc{
    int gpio;               /*io编号*/
    int irqnum;              /*中断号*/
    unsigned char value;    /*键值*/
    char name[10];          /*按键名称*/
    irqreturn_t (*irq_handler_t)(int, void *);  /*中断处理函数*/
};

struct key_dev{
    dev_t devid;
    int major;
    int minor;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
    struct irq_keydesc irqkey[KEY_NUM];
    struct timer_list timer;

    atomic_t keyvalue;       /*按键值*/
    atomic_t releasekey;     /*按键按下的标志*/

    wait_queue_head_t r_wait;
};

struct key_dev key;

/*中断回调函数*/ 
static irqreturn_t keyirq_handler_t(int irq, void *dev_id)
{
    struct key_dev *dev = dev_id;

    dev->timer.data = (unsigned long)dev_id;
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(15));
    return IRQ_RETVAL(IRQ_HANDLED);
}

/*定时器回调函数*/ 
static void timer_func(unsigned long arg){
    int value = 0;
    struct key_dev *dev = (struct key_dev *)arg;

    value = gpio_get_value(dev->irqkey[0].gpio);
    if(value == 0){     /*按下*/
        atomic_set(&dev->keyvalue, dev->irqkey[0].value);
    }else if(value == 1){   /*释放*/
        if(atomic_read(&dev->keyvalue) == dev->irqkey[0].value){
            atomic_set(&dev->keyvalue, 0x80 | dev->irqkey[0].value);
            atomic_set(&dev->releasekey, 1);
        }
    }
    if(atomic_read(&dev->releasekey)){
        wake_up_interruptible(&dev->r_wait);
    }
}
/*
* @description : keyio初始化
* @return : 0 初始化成功，<0 初始化失败
*/
static int keyio_init(struct key_dev *dev)
{
    int ret = 0;
    int i = 0;
    /*获取设备节点*/
    dev->nd = of_find_node_by_path("/key");
    if(dev->nd == NULL){
        ret =  -EINVAL;
        goto fail_get_nd;
    }

    /*获取key的GPIO*/
    for(i = 0; i< KEY_NUM; i++){
        dev->irqkey[i].gpio = of_get_named_gpio(dev->nd, "key-gpios", i);
        if(dev->irqkey[i].gpio < 0){
            printk("Get irqkey[%d] failed!\r\n", i);
            ret = -EINVAL;
            goto fail_get_gpio;
        }
        printk("irqkey[%d] gpio num = %d\r\n", i, dev->irqkey[i].gpio);
    }

    /*向内核申请io*/
    for(i = 0; i< KEY_NUM; i++){
        memset(dev->irqkey[i].name, 0, sizeof(dev->irqkey[i].name));
        /*格式化name*/ 
        sprintf(dev->irqkey[i].name, "KEY%d", i); 
        ret = gpio_request(dev->irqkey[i].gpio, dev->irqkey[i].name);
        if(ret){
            printk("Failed to request the irqkey[%d] gpio\r\n", i);
            ret = -EINVAL;
            goto fail_reqs_gpio;
        }
    }

    /*设置key的GPIO为输入*/
    for(i = 0; i< KEY_NUM; i++){
        ret = gpio_direction_input(dev->irqkey[i].gpio);  /*设置key为输入*/ 
        if(ret){
            ret = -EINVAL;
            goto fail_setup_gpio;
        }
    }

    /*获取key的GPIO的中断号*/
    for(i = 0; i< KEY_NUM; i++){
        /*方法一*/ 
        dev->irqkey[i].irqnum = gpio_to_irq(dev->irqkey[i].gpio);
        // /*方法二*/ 
        // dev->irqkey[i].irqnum = irq_of_parse_irq(dev->nd, i);
        printk("irqkey[%d].irqnum = %d\r\n", i, dev->irqkey[i].irqnum);
    }

    /*初始化中断*/
    dev->irqkey[0].irq_handler_t = keyirq_handler_t;
    dev->irqkey[0].value = KEY0_VALUE;
    for(i = 0; i< KEY_NUM; i++){
        ret = request_irq(dev->irqkey[i].irqnum,dev->irqkey[i].irq_handler_t, 
                            IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, 
                            dev->irqkey[i].name, &key);
        if(ret){
            printk("irq %d request failed!\r\n", dev->irqkey[i].irqnum);
            ret = -EINVAL;
            goto fail_requset_irq;
        }
    }

    /* 初始化定时器 */
    init_timer(&dev->timer); 
    dev->timer.function = timer_func;

    return 0;

fail_requset_irq:
fail_setup_gpio:
    /*释放key的GPIO*/
    for(i = 0; i< KEY_NUM; i++){ 
        gpio_free(key.irqkey[i].gpio);
    }    
fail_reqs_gpio:    
fail_get_gpio:    
fail_get_nd:
    return ret;
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

    return ret;
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
    unsigned char keyvalue;
    unsigned char releasekey;
    struct key_dev *dev = filp->private_data;


    if(filp->f_flags & O_NONBLOCK) {                    /*如果是非阻塞访问*/
        if(atomic_read(&dev->releasekey) == 0) {        /*如果没有按键按下*/
            return -EAGAIN;
        }
    }else
    {
        /*等待事件*/
        wait_event_interruptible(dev->r_wait, atomic_read(&dev->releasekey));

    }
    
    keyvalue = atomic_read(&dev->keyvalue);
    releasekey = atomic_read(&dev->releasekey);

    if(releasekey){  /*按键有效*/
        if(keyvalue & 0x80){
            keyvalue &= ~0x80;
            ret = copy_to_user(buf, &keyvalue, sizeof(keyvalue));
        }else{
            return -EINVAL;
        }
        atomic_set(&dev->releasekey, 0);    /*按下标志清零*/
    }else{
        return -EINVAL;
    }
    return ret;
}

static unsigned int key_poll (struct file *flip,  poll_table *wait)
{
    int mask = 0;
    struct key_dev *dev = flip->private_data;

    poll_wait(flip, &dev->r_wait, wait);

    if(atomic_read(&dev->releasekey)){      /*如果按键按下，返回pollin*/
        mask = POLLIN | POLLRDNORM;
    }
    return mask;
}
/*
* @description : 关闭/释放设备
* @param - filp : 要关闭的设备文件(文件描述符)
* @return : 0 成功;其他 失败
*/
static int key_release (struct inode *inode, struct file *filp)
{
    return 0;
}

static struct file_operations key_fops = {
    .owner = THIS_MODULE,
    .open = key_open,
    .read = key_read,
    .release = key_release,
    .poll = key_poll,
};


static int __init mykey_init(void)
{
    int ret = 0;

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

    /*初始化IO*/
    ret =  keyio_init(&key);
    if(ret < 0){
        goto fail_keyio_init;
    }

    /*初始化原子变量*/
    atomic_set(&key.keyvalue, INVAKEY);
    atomic_set(&key.releasekey, 0);

    /*初始化等待队列头*/
    init_waitqueue_head(&key.r_wait); 

    printk("key_init()\r\n");
    return 0;

fail_keyio_init:
    device_destroy(key.class, key.devid); 
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
    int i = 0;
    /*删除定时器*/
    del_timer_sync(&key.timer); 
    /*释放中断*/
    for(i = 0; i< KEY_NUM; i++){ 
        free_irq(key.irqkey[i].irqnum, &key);
    }
    /*释放key的GPIO*/
    for(i = 0; i< KEY_NUM; i++){ 
        gpio_free(key.irqkey[i].gpio);
    }
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