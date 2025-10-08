# ======================================
# Makefile kernel for bdev_snapshot LKM
# ======================================

# Name of the module
obj-m := bdev_snapshot.o

# Source files
bdev_snapshot-objs := bdev_snapshot_main.o \
		      snap_auth.o \
		      cdev_snap.o \
		      bdev_kprobe.o \
		      snap_ioctl.o \
		      bdev_list.o \
		      snap_store.o \
		      snap_restore.o \
		      snap_utils.o

EXTRA_CFLAGS := -I$(CURDIR)/include

# Default build target: compile the module
all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) modules EXTRA_CFLAGS="$(EXTRA_CFLAGS)"

# Clean build artifacts
clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) clean	

