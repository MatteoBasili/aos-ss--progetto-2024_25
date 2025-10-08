# ==============================================
# Makefile for bdev_snapshot user-space program
# ==============================================

all:
	gcc -Wall -Wextra -Werror -Wformat-security -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
	    -I../src/include/ -o bdev_snap_app bdev_snap_app.c
	
clean:
	rm -rf bdev_snap_app
