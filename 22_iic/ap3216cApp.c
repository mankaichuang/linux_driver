#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"


int main(int argc, char *argv[])
{   
    int fd, err;
    char *filename;
    unsigned short data[3];
    unsigned short ir,als,ps;

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
        err = read(fd, data, sizeof(data));
        if(err == 0) {
            ir = data[0];
            als = data[1];
            ps = data[2];
            printf("ir = %d, als = %d, ps = %d\r\n", ir, als, ps);
        }
        usleep(200000);

    }
 
    err = close(fd);
    if(err < 0)
    {   
        printf("file %s close failed!\r\n",argv[1]);
        return -1;
    }
    return 0;
}