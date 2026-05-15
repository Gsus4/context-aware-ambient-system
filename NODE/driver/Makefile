obj-m += smartlight_drv.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)
CC   ?= gcc
CFLAGS_USER ?= -O2 -Wall

.PHONY: all clean modules test tools

all: modules tools

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

tools: test_ioctl

test_ioctl: test_ioctl.c smartlight_ioctl.h
	$(CC) $(CFLAGS_USER) -o $@ test_ioctl.c

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f test_ioctl
