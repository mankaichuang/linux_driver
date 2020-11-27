#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

#define KEY0_VALUE      0XF0

int main(int argc, char *argv[])
{   
    int cnt = 0;
    int fd, retvalue;
    char *filename;
    unsigned char keyvalue;

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
        retvalue = read(fd, &keyvalue, sizeof(keyvalue));
        if(retvalue < 0){

        }else {
            if(keyvalue)
                printf("KEY0 Press, value = %#X\r\n",keyvalue);
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