#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "snapshot.h"

int main(int argc, char *argv[]) {
    int fd;
    if (argc < 2) {
        printf("Uso: %s [activate|deactivate]\n", argv[0]);
        return 1;
    }

    fd = open("/dev/snapshot", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (strcmp(argv[1], "activate") == 0) {
        if (ioctl(fd, SNAPSHOT_ACTIVATE, SNAPSHOT_PASSWD) < 0)
            perror("ioctl activate");
        else
            printf("Snapshot attivato!\n");
    } else if (strcmp(argv[1], "deactivate") == 0) {
        if (ioctl(fd, SNAPSHOT_DEACTIVATE, SNAPSHOT_PASSWD) < 0)
            perror("ioctl deactivate");
        else
            printf("Snapshot disattivato!\n");
    } else {
        printf("Comando non valido: %s\n", argv[1]);
    }

    close(fd);
    return 0;
}

