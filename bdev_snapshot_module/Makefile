obj-m += bdev_snapshot.o
bdev_snapshot-objs += bdev_snapshot_main.o bdev_ops.o bdev_password.o bdev_list.o bdev_kprobe.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	
load-snap-mod:
	insmod bdev_snapshot.ko

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	
remove-snap-mod:
	rmmod bdev_snapshot
