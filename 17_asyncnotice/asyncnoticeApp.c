#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "signal.h"
#include "fcntl.h"
#include "poll.h"

#define KEY0_VALUE      0XF0

static int fd = 0;      /*全局变量文件描述符*/

static void sigio_signal_func(int signum)
{
    int err = 0;
    unsigned char keyvalue = 0;

    err = read(fd, &keyvalue, sizeof(keyvalue));
    if(err < 0){
        /*读取错误*/
    }else {
        if(keyvalue)
            printf("sigio signal! KEY0 Press, value = %#X\r\n",keyvalue);
        }    
}

int main(int argc, char *argv[])
{   
    int flags = 0;
    int retvalue = 0;
    char *filename;

    if(argc != 2)
    {
        printf("Error param!\r\n");
        return -1;
    }

    filename = argv[1];
    /*打开设备文件*/ 
    fd = open(filename, O_RDWR);
    if(fd < 0)
    {
        printf("file %s open failed!\r\n", argv[1]);
        return -1;
    }

    /*设置信号SIGIO的处理函数*/
    signal(SIGIO, sigio_signal_func);

    fcntl(fd, F_SETOWN, getpid());      /*将当前进程的PID告诉给内核*/
    flags = fcntl(fd, F_GETFD);          /*获取当前进程状态*/
    fcntl(fd,F_SETFL, flags | FASYNC);  /*设置进程 启用异步通知*/

    while(1){
        sleep(2);
    }
    
    retvalue = close(fd);
    if(retvalue < 0)
    {   
        printf("file %s close failed!\r\n",argv[1]);
        return -1;
    }
    return 0;
}