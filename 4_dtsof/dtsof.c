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

static int __init dtsof_init(void)
{
    int ret = 0;
    struct device_node *bl_nd;
    struct property *comppro;
    const char  *status_str;
    u32 df_value = 0;
    u32 elemsize = 0;
    u32 *brval;
    int i;

    /*1、找到backlight节点，路径是：/backlight*/
    bl_nd = of_find_node_by_path("/backlight");
    if(bl_nd == NULL)
    {
        ret = -EINVAL;
        goto fail_find_nd;
    }

    /*2、获取属性*/
    comppro = of_find_property(bl_nd,"compatible",NULL);
    if(comppro == NULL)
    {
        ret = -EINVAL;
        goto fail_find_pro;
    }
    else
    {
        printk("compatible = %s\r\n",(char *)comppro->value);
    }

    /*3、获取字符串属性*/ 
    ret = of_property_read_string(bl_nd, "status", &status_str);
    if(ret < 0)
    {
        goto fail_read_str;
    }
    else
    {
        printk("status = %s\r\n", status_str);
    }

    /*4、获取U32属性*/ 
    ret = of_property_read_u32(bl_nd, "default-brightness-level", &df_value);
    if(ret < 0)
    {
        goto fail_read_u32;
    }
    else
    {
        printk("default-brightness-level = %d\r\n", df_value);
    }

    /*5、获取属性元素的size*/ 
    elemsize = of_property_count_elems_of_size(bl_nd, "brightness-levels", sizeof(u32));
    if(elemsize < 0)
    {
        ret = -EINVAL;
        goto fail_read_elems_size;
    }
    else
    {
        printk("brightness-levels size = %d\r\n", elemsize);
    }

    /*申请内存*/ 
    brval = kmalloc(elemsize * sizeof(u32), GFP_KERNEL);
    if(!brval)
    {
        ret = -EINVAL;
        goto fail_mem;
    }

    /*获取数组*/ 
    ret = of_property_read_u32_array(bl_nd, "brightness-levels", brval, elemsize);
    if(ret < 0)
    {
        goto fail_read_array;
    }
    else
    {
        printk("brightness-levels =[ ");
        for(i=0; i<elemsize; i++)
        {
            printk("%d ",brval[i]);
        }
        printk("]\r\n");
    }

    return 0;

fail_read_array:
    kfree(brval);   /*释放内存*/
fail_mem:
    printk("kmalloc failed\r\n");
fail_read_elems_size:
fail_read_u32:
fail_read_str:
fail_find_pro:
fail_find_nd:
    return ret;
}

static void __exit dtsof_exit(void)
{
    printk("dtsof_exit\r\n");
}

/*注册模块入口和出口*/
module_init(dtsof_init);
module_exit(dtsof_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("mankc");