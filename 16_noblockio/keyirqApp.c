#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include <sys/select.h>
#include <sys/time.h>
#include <poll.h>



#define KEY0_VALUE      0XF0

int main(int argc, char *argv[])
{   
    int cnt = 0;
    int fd, retvalue;
    char *filename;
    unsigned char keyvalue;
    struct pollfd fds;
    fd_set readfds;
    struct timeval timeout;

    if(argc != 2)
    {
        printf("Error param!\r\n");
        return -1;
    }

    filename = argv[1];
    /*打开设备文件*/ 
    fd = open(filename, O_RDWR|O_NONBLOCK);
    if(fd < 0)
    {
        printf("file %s open failed!\r\n", argv[1]);
        return -1;
    }

#if 0
    while(1){
        FD_ZERO(&readfds);      /*对readfds清零*/
        FD_SET(fd, &readfds);   /*将fd配置到readfds*/

        /*构造超时时间*/
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000;    /*500ms*/

        retvalue = select(fd+1, &readfds, NULL, NULL, &timeout);
        switch(retvalue){
            case 0:             /*超时*/
                printf("select timeout!\r\n");
                break;
            case -1:            /*error*/
                printf("select error!\r\n");
                break;
            default:            /*可以读取数据*/
                if(FD_ISSET(fd, &readfds)) {
                    retvalue = read(fd, &keyvalue, sizeof(keyvalue));
                    if(retvalue < 0){
                        printf("read data failed!\r\n");
                    }else {
                        if(keyvalue)
                            printf("KEY0 Press, value = %#X\r\n",keyvalue);
                    }
                }
        }
    }
#endif

    /*构造fds结构体*/
    fds.fd = fd;
    fds.events = POLLIN;

    while(1){
        retvalue = poll(&fds, 1, 500);

        if(retvalue) {      /*数据有效*/
            retvalue = read(fd, &keyvalue, sizeof(keyvalue));
            if(retvalue < 0){
                printf("read data failed!\r\n");
            }else {
                if(keyvalue)
                    printf("KEY0 Press, value = %#X\r\n",keyvalue);
            }
        }else if(retvalue == 0) {       /*超时*/
            printf("poll timeout!\r\n");
        }else if(retvalue < 0){
            printf("poll error!\r\n");
        }
    }

    retvalue = close(fd);
    if(retvalue < 0)
    {   
        printf("file %s close failed!\r\n",argv[1]);
        return -1;
    }
    return 0;
}