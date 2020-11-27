#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "sys/ioctl.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

#define CLOSE_CMD       _IO(0xEF, 1)
#define OPEN_CMD        _IO(0xEF, 2)       
#define SET_CMD         _IOW(0xEF, 3, int)

int main(int argc, char *argv[])
{   
    int fd, ret;
    char *filename;
    unsigned int cmd;
    unsigned int arg;
    unsigned char str[100];

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

    while(1){
        printf("intput cmd:");
        ret = scanf("%d", &cmd);
        // if(ret != 1){
        //     gets(str);   /*防止卡死*/
        // }

        if(cmd ==1){
            ioctl(fd, CLOSE_CMD, &arg);
        }else if(cmd == 2){
            ioctl(fd, OPEN_CMD, &arg);
        }else if(cmd ==3){
            printf("input period:");
            ret = scanf("%d", &arg);
            // if(ret != 1){
            //     gets(str);
            // }
            ioctl(fd, SET_CMD, &arg);
        }else {
            break;
        }
    }
    ret = close(fd);
    if(ret < 0)
    {   
        printf("file %s close failed!\r\n",argv[1]);
        return -1;
    }
    return 0;
}