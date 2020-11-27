#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

int main(int argc, char *argv[])
{   
    int cnt = 0;
    int fd, retvalue;
    char *filename;
    unsigned char databuf[1];

    if(argc != 3)
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

    databuf[0] = atoi(argv[2]);

    retvalue = write(fd, databuf, sizeof(databuf));
    if(retvalue < 0)
    {
        printf("led ctl failed!\r\n");
        close(fd);
        return -1;
    }

    while(1){
        sleep(5);
        cnt++;
        printf("Running times: %d\r\n", cnt);
        if(cnt >=5)
            break;
    }
    retvalue = close(fd);
    if(retvalue < 0)
    {   
        printf("file %s close failed!\r\n",argv[1]);
        return -1;
    }
    return 0;
}