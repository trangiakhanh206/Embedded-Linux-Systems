#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "asgn1_ioctl.h"

int main(void)
{
    int fd;
    int mode = 0;
    struct asgn1_stats stats;
    char version[64] = {0};

    fd = open("/dev/asgn1", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ioctl(fd, ASGN1_GET_STATS, &stats) < 0) {
        perror("GET_STATS");
        close(fd);
        return 1;
    }

    printf("GET_STATS:\n");
    printf("  open_count      = %d\n", stats.open_count);
    printf("  write_count     = %d\n", stats.write_count);
    printf("  read_count      = %d\n", stats.read_count);
    printf("  buffer_len      = %d\n", stats.buffer_len);
    printf("  last_write_size = %d\n", stats.last_write_size);

    if (ioctl(fd, ASGN1_SET_MODE, &mode) < 0) {
        perror("SET_MODE");
        close(fd);
        return 1;
    }

    printf("SET_MODE = %d: OK\n", mode);

    if (ioctl(fd, ASGN1_GET_VERSION, version) < 0) {
        perror("GET_VERSION");
        close(fd);
        return 1;
    }

    printf("GET_VERSION: %s\n", version);

    if (ioctl(fd, ASGN1_RESET_BUFFER) < 0) {
        perror("RESET_BUFFER");
        close(fd);
        return 1;
    }

    printf("RESET_BUFFER: OK\n");

    close(fd);
    return 0;
}
