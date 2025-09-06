#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "include/uapi/bdev_snapshot_uapi.h"

/* --- Secure memzero per user-space --- */
static void secure_memzero(void *v, size_t n)
{
    volatile unsigned char *p = v;
    while (n--) *p++ = 0;
}

/* --- Set snapshot password (globale, non device-specific) --- */
static void do_setpw(int fd, const char *pw)
{
    struct pw_args args;
    memset(&args, 0, sizeof(args));
    strncpy(args.password, pw, SNAP_PASSWORD_MAX - 1);

    if (ioctl(fd, SNAP_SETPW, &args) < 0) {
        fprintf(stderr, "SNAP_SETPW failed: %s\n", strerror(errno));
    } else {
        printf("SNAP_SETPW ok\n");
    }

    secure_memzero(args.password, sizeof(args.password));
}

/* --- Activate / Deactivate snapshot per device --- */
static void do_snap(int fd, unsigned long code, const char *dev, const char *pw)
{
    struct snap_args args;
    memset(&args, 0, sizeof(args));
    strncpy(args.dev_name, dev, DEV_NAME_LEN - 1);
    if (pw)
        strncpy(args.password, pw, SNAP_PASSWORD_MAX - 1);

    if (ioctl(fd, code, &args) < 0) {
        fprintf(stderr, "%s failed: %s\n",
                (code == SNAP_ACTIVATE) ? "SNAP_ACTIVATE" : "SNAP_DEACTIVATE",
                strerror(errno));
    } else {
        printf("%s ok\n", (code == SNAP_ACTIVATE) ? "SNAP_ACTIVATE" : "SNAP_DEACTIVATE");
    }

    if (pw)
        secure_memzero(args.password, sizeof(args.password));
}

int main(int argc, char **argv)
{
    const char *node = "/dev/bdev_snapctl";

    if (argc < 2) {
        fprintf(stderr, "Usage: %s setpw <password> | activate <dev> <password> | deactivate <dev> <password>\n", argv[0]);
        return 1;
    }

    int fd = open(node, O_RDWR);
    if (fd < 0) {
        perror("open /dev/bdev_snapctl");
        return 1;
    }
    
    /* --- Drop privileges after open --- */
    /*if (setuid(1000) != 0) {
        perror("setuid");
        close(fd);
        return 1;
    }
    printf("Privileges dropped, running as UID=%d\n", getuid());*/

    if (strcmp(argv[1], "setpw") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Password required for setpw\n");
            close(fd);
            return 1;
        }
        do_setpw(fd, argv[2]);
    }
    else if (strcmp(argv[1], "activate") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Device and password required for activate\n");
            close(fd);
            return 1;
        }
        do_snap(fd, SNAP_ACTIVATE, argv[2], argv[3]);
    }
    else if (strcmp(argv[1], "deactivate") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Device and password required for deactivate\n");
            close(fd);
            return 1;
        }
        do_snap(fd, SNAP_DEACTIVATE, argv[2], argv[3]);
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
    }

    close(fd);
    return 0;
}

