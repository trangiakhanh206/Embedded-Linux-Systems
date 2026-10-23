#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define SMS_IOCTL_MAGIC 'S'
#define SMS_SET_INTERVAL _IOW(SMS_IOCTL_MAGIC, 1, unsigned int)

int main(void)
{
    int fd;
    unsigned int interval = 500;

    fd = open("/dev/sms_sensor", O_RDONLY);

    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ioctl(fd, SMS_SET_INTERVAL, &interval) < 0) {
        perror("ioctl");
        close(fd);
        return 1;
    }

    printf("SMS_SET_INTERVAL success: %u ms\n", interval);

    close(fd);

    return 0;
}
