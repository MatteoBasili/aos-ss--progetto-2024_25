#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "bdev_snapshot.h"

static void try_ioctl(int fd, unsigned long code, const char *name, const char *dev, const char *pw)
{
    struct snap_args args;
    memset(&args, 0, sizeof(args));
    snprintf(args.dev_name, sizeof(args.dev_name), "%s", dev);
    snprintf(args.password, sizeof(args.password), "%s", pw);

    if (ioctl(fd, code, &args) < 0) {
        fprintf(stderr, "%s failed: %s\n", name, strerror(errno));
    } else {
        printf("%s ok\n", name);
    }
}

int main(int argc, char **argv)
{
    const char *node = "/dev/bdev_snapshot";
    const char *dev  = (argc > 1) ? argv[1] : "/dev/loop0";
    const char *pw   = (argc > 2) ? argv[2] : "secret";

    int fd = open(node, O_RDWR);
    if (fd < 0) {
        perror("open /dev/bdev_snapshot");
        return 1;
    }

    printf("Using device node: %s\n", node);
    printf("Target block dev : %s\n", dev);

    try_ioctl(fd, SNAP_ACTIVATE,   "SNAP_ACTIVATE",   dev, pw);
    try_ioctl(fd, SNAP_DEACTIVATE, "SNAP_DEACTIVATE", dev, pw);

    close(fd);
    return 0;
}

