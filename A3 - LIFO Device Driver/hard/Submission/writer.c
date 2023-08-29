#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>

int main (int argc , char * argv [])
{
    char device [100];
    char user_msg [100];

    strcpy ( device , argv [1]) ;
    strcpy ( user_msg , argv [2]);

    // device name /dev/mychardev-1
    int fd = open(device , O_WRONLY ) ;
    printf("fd = %d\n", fd);

    printf("writing %s\n", user_msg);
    write(fd, user_msg, strlen(user_msg));

}