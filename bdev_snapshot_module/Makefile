# Nome del modulo
obj-m := bdev_snapshot.o

# Sorgenti del modulo
bdev_snapshot-objs := \
	src/bdev_snapshot_main.o \
	src/bdev_ops.o \
	src/bdev_auth.o \
	src/bdev_list.o \
	src/bdev_kprobe.o

# Aggiungi il path include/
EXTRA_CFLAGS := -I$(PWD)/include

# Compilazione di default
all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules EXTRA_CFLAGS="$(EXTRA_CFLAGS)"

# Carica il modulo
load-snap-mod:
	sudo insmod bdev_snapshot.ko

# Rimuove il modulo
remove-snap-mod:
	sudo rmmod bdev_snapshot

# Pulizia build
clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

