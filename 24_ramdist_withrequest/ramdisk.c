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
#include <linux/hdreg.h>

#define RAMDISK_SIZE        (2 * 1024 * 1024)       /*容量大小位2MB*/
#define RAMDISK_NAME        "ramdisk"               /*名字*/
#define RAMDISK_MINOR       3                       /*表示三个磁盘分区，不是次设备号是3*/

/*ramdisk设备结构体*/
struct ramdisk_dev{
    int major;                          /*主设备号*/
    unsigned char *ramdiskbuf;          /*ramdisk内存空间，用于模拟块设备*/
    spinlock_t lock;                      /*自旋锁*/
    struct gendisk *gendisk;            /*gendisk*/
    struct request_queue *queue;        /*请求队列*/
}; 

struct ramdisk_dev ramdisk;

static void ramdisk_transfer(struct request *req)
{
    unsigned long start = blk_rq_pos(req) << 9;     /*blk_rq_pos获取到的是扇区地址，左移9位转换为字节地址*/
    unsigned long len = blk_rq_cur_bytes(req);      /*大小*/    
    /* bio 中的数据缓冲区
    * 读：从磁盘读取到的数据存放到 buffer 中
    * 写： buffer 保存这要写入磁盘的数据
    */
    void *buffer = bio_data(req->bio);

    if(rq_data_dir(req) == READ)
        memcpy(buffer, ramdisk.ramdiskbuf + start, len);
    else if(rq_data_dir(req) == WRITE)
        memcpy(ramdisk.ramdiskbuf + start, buffer, len);
}

static void ramdisk_request_fn (struct request_queue *q)
{
    int err = 0;
    struct request *req;

    /*循环处理队列中的每一个请求*/
    req = blk_fetch_request(q);
    while(req != NULL) {

        /*针对请求做具体的传输处理*/
        ramdisk_transfer(req);

        /*判断是否为最后一个请求，如果不是的话就获取下一个请求*/
        /*循环处理完请求队列中的所有请求*/
        if(!__blk_end_request_cur(req, err))
            req = blk_fetch_request(q);
    }
}

static int ramdisk_open (struct block_device *dev, fmode_t mode)
{
    printk("ramdisk_open!\r\n");
    return 0;
}

static void ramdisk_release (struct gendisk *disk, fmode_t mode)
{
    printk("ramdisk_release!\r\n");
}

static int ramdisk_getgeo(struct block_device *dev, struct hd_geometry *geo)
{   
    /*这是相对于机械硬盘的概念*/
    geo->heads = 2;                             /*磁头*/
    geo->cylinders = 32;                        /*柱面*/
    geo->sectors = RAMDISK_SIZE/(2*32*512);     /*磁道上的扇区数量*/

    return 0;
}
static struct block_device_operations ramdisk_ops = {
    .owner = THIS_MODULE,
    .open = ramdisk_open,
    .release = ramdisk_release,
    .getgeo = ramdisk_getgeo,
};
static int __init ramdisk_init(void)
{
    int ret = 0;

    /*1、申请用于ramdisk的内存*/
    ramdisk.ramdiskbuf = kzalloc(RAMDISK_SIZE, GFP_KERNEL);
    if(ramdisk.ramdiskbuf == NULL){
        printk("kzalloc memery failed!\r\n");
        ret = -ENOMEM;
        goto fail_alloc_mem;
    }

    /*2、初始化自旋锁*/
    spin_lock_init(&ramdisk.lock);

    /*3、注册块设备*/
    ramdisk.major = register_blkdev(0, RAMDISK_NAME);       /*参数位0时自动分配主设备号*/
    if(ramdisk.major < 0) {
        printk("register blkdev failed!\r\n");
        ret = ramdisk.major;
        goto fail_reg_blk;
    }else {
        printk("ramdisk major = %d\r\n",ramdisk.major);
    }

    /*4、分配并初始化gendisk*/
    ramdisk.gendisk = alloc_disk(RAMDISK_MINOR);
    if(!ramdisk.gendisk){
        ret = -EINVAL;
        goto fail_alloc_gendisk;
    }

    /*5、分配并初始化请求队列*/
    ramdisk.queue = blk_init_queue(ramdisk_request_fn, &ramdisk.lock);
    if(!ramdisk.queue){
        ret = -ENOMEM;
        goto fail_init_queue;
    }

    /*6、初始化gendisk*/
    ramdisk.gendisk->major = ramdisk.major;             /*主设备号*/
    ramdisk.gendisk->first_minor = 0;                   /*起始次设备号*/
    ramdisk.gendisk->fops = &ramdisk_ops;                /*操作函数*/
    ramdisk.gendisk->private_data = &ramdisk;           /*私有数据*/
    ramdisk.gendisk->queue = ramdisk.queue;             /*请求队列*/
    sprintf(ramdisk.gendisk->disk_name, RAMDISK_NAME);  /*设置disk_name*/
    set_capacity(ramdisk.gendisk, RAMDISK_SIZE/512);    /*设备容量（单位为扇区）*/

    /*7、添加（注册）gendisk*/
    add_disk(ramdisk.gendisk);

    return 0;

fail_init_queue:
    put_disk(ramdisk.gendisk);                      /*释放gendisk*/
fail_alloc_gendisk:
    unregister_blkdev(ramdisk.major,RAMDISK_NAME);  /*注销块设备*/
fail_reg_blk:
    kfree(ramdisk.ramdiskbuf);                      /*释放内存*/
fail_alloc_mem:
    return ret;
}

static void __exit ramdisk_exit(void)
{
    /*注销gendisk*/
    del_gendisk(ramdisk.gendisk);
    /*清除请求队列*/
    blk_cleanup_queue(ramdisk.queue);
    /*释放gendisk*/
    put_disk(ramdisk.gendisk);
    /*注销块设备*/
    unregister_blkdev(ramdisk.major, RAMDISK_NAME);
    /*释放内存*/
    kfree(ramdisk.ramdiskbuf);
}


module_init(ramdisk_init);
module_exit(ramdisk_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("mankc");