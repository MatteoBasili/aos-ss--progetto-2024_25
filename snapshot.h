#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include <linux/ioctl.h>

#define SNAPSHOT_MAGIC 's'

// Comandi ioctl
#define SNAPSHOT_ACTIVATE   _IOW(SNAPSHOT_MAGIC, 1, char*)
#define SNAPSHOT_DEACTIVATE _IOW(SNAPSHOT_MAGIC, 2, char*)

#define SNAPSHOT_PASSWD "my_secret_token"  // token hardcoded per MVP

#endif

