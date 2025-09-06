# Nome del modulo
obj-m := bdev_snapshot.o

# Sorgenti del modulo
bdev_snapshot-objs := \
	bdev_snapshot_main.o \
	bdev_ops.o \
	bdev_auth.o \
	bdev_list.o \
	bdev_kprobe.o

# Aggiungi il path include/
EXTRA_CFLAGS := -I$(PWD)/include

# Compilazione di default
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules EXTRA_CFLAGS="$(EXTRA_CFLAGS)"

# Carica il modulo
load-snap-mod:
	sudo insmod bdev_snapshot.ko

# Rimuove il modulo
remove-snap-mod:
	sudo rmmod bdev_snapshot

# Pulizia build
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

