#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include <linux/input.h>

/*定义input_event结构体变量*/
static struct input_event inputevent;

int main(int argc, char *argv[])
{   
    int fd, err;
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

    while(1){
        err = read(fd, &inputevent, sizeof(inputevent));
        if(err > 0){
            switch(inputevent.type){
                case EV_KEY:
                    if(inputevent.code < BTN_MISC){
                        printf("key %d %s\r\n", inputevent.code, inputevent.value ? "press" : "release");
                    }else{
                        printf("button %d %s\r\n", inputevent.code, inputevent.value ? "press" : "release");
                    }
                    break;
                case EV_SYN:
                    break;
            }
        }else {
            printf("read input_event data failde!\r\n");
        }
    }
    
    err = close(fd);
    if(err < 0)
    {   
        printf("file %s close failed!\r\n",argv[1]);
        return -1;
    }
    return 0;
}