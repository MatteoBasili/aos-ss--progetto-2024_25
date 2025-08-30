obj-m += bdev_snapshot.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	
load-snap-mod:
	insmod bdev_snapshot.ko

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
