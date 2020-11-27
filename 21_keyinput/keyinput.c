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

#define KEYINPUT_NAME       "keyinput"
#define KEY_NUM             1

struct irq_keydesc{
    int gpio;               /*io编号*/
    int irqnum;              /*中断号*/
    unsigned char value;    /*键值*/
    char name[10];          /*按键名称*/
    irqreturn_t (*irq_handler_t)(int, void *);  /*中断处理函数*/
};

struct keyinput_dev{
    struct device_node *nd;
    struct irq_keydesc irqkey[KEY_NUM];
    struct timer_list timer;

    struct input_dev *inputdev;
};

struct keyinput_dev keyinputdev;

/*中断回调函数*/ 
static irqreturn_t keyinput_handler_t(int irq, void *dev_id)
{
    struct keyinput_dev *dev = dev_id;

    dev->timer.data = (unsigned long)dev_id;
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(20));
    return IRQ_RETVAL(IRQ_HANDLED);
}

/*定时器回调函数*/ 
static void timer_func(unsigned long arg){
    int value = 0;
    struct keyinput_dev *dev = (struct keyinput_dev *)arg;

    value = gpio_get_value(dev->irqkey[0].gpio);
    if(value == 0){     /*按下*/
        /*上报数据*/
        input_event(dev->inputdev,EV_KEY, KEY_0, 1);
        input_sync(dev->inputdev);
    }else if(value == 1){   /*释放*/
        /*上报数据*/
        input_event(dev->inputdev,EV_KEY, KEY_0, 0);
        input_sync(dev->inputdev);
    }
}
/*
* @description : keyio初始化
* @return : 0 初始化成功，<0 初始化失败
*/
static int keyio_init(struct keyinput_dev *dev)
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
    dev->irqkey[0].irq_handler_t = keyinput_handler_t;
    dev->irqkey[0].value = KEY_0;
    for(i = 0; i< KEY_NUM; i++){
        ret = request_irq(dev->irqkey[i].irqnum,dev->irqkey[i].irq_handler_t, 
                            IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, 
                            dev->irqkey[i].name, &keyinputdev);
        if(ret){
            printk("irq %d request failed!\r\n", dev->irqkey[i].irqnum);
            ret = -EINVAL;
            goto fail_requset_irq;
        }
    }

    /* 初始化定时器 */
    init_timer(&dev->timer); 
    dev->timer.function = timer_func;

    /*注册input_dev*/

    /*申请input_dev*/
    dev->inputdev = input_allocate_device();
    if(dev->inputdev == NULL){
        ret = -EINVAL;
        goto fail_alloc_input;
    }

    /*初始化inputdev*/
    dev->inputdev->name = KEYINPUT_NAME;
    __set_bit(EV_KEY, dev->inputdev->evbit);        /*按键事件*/
    __set_bit(EV_REP, dev->inputdev->evbit);        /*重复事件*/
    __set_bit(KEY_0, dev->inputdev->keybit);        /*按键值*/

    /*注册*/
    ret = input_register_device(dev->inputdev);
    if(ret){
        goto fail_reg_inputdev;
    }
    return 0;

fail_reg_inputdev:
    input_free_device(dev->inputdev);
fail_alloc_input:
    /*删除定时器*/
    del_timer_sync(&dev->timer);
    /*释放中断*/
    for(i = 0; i< KEY_NUM; i++){ 
        free_irq(dev->irqkey[i].irqnum, &dev);
    }
fail_requset_irq:
fail_setup_gpio:
    /*释放key的GPIO*/
    for(i = 0; i< KEY_NUM; i++){ 
        gpio_free(dev->irqkey[i].gpio);
    }    
fail_reqs_gpio:    
fail_get_gpio:    
fail_get_nd:
    return ret;
}

static int __init mykey_init(void)
{
    int ret = 0;

    /*初始化IO*/
    ret =  keyio_init(&keyinputdev);
    if(ret < 0){
        return ret;
    }

    printk("key_init()\r\n");
    return 0;

}

static void __exit mykey_exit(void)
{
    int i = 0;
    /*删除定时器*/
    del_timer_sync(&keyinputdev.timer); 
    /*释放中断*/
    for(i = 0; i< KEY_NUM; i++){ 
        free_irq(keyinputdev.irqkey[i].irqnum, &keyinputdev);
    }
    /*释放key的GPIO*/
    for(i = 0; i< KEY_NUM; i++){ 
        gpio_free(keyinputdev.irqkey[i].gpio);
    }
    /*注销input_dev*/
    input_unregister_device(keyinputdev.inputdev);
    /*释放input_dev*/
    input_free_device(keyinputdev.inputdev);
    printk("key_exit()\r\n");
}


module_init(mykey_init);
module_exit(mykey_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("mankc");