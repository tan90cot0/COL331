#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>

int main (int argc , char * argv [])
{
    char device [100];
    char user_msg [100];

    strcpy ( device , argv [1]) ;

    // device name /dev/mychardev-0
    int fd = open(device , O_RDONLY ) ;

    printf("Reading\n");
    read(fd, user_msg, 16);
    printf("String read is %s\n", user_msg);

}