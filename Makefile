#
# Makefile for sensor chip drivers.
#

obj-m	:= smo8800.o

PWD	:= $(shell pwd)
KDIR	:= /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	rm -f *~ Module.symvers Module.markers modules.order
	make -C $(KDIR) M=$(PWD) clean

install: all
	make -C $(KDIR) M=$(PWD) modules_install
	depmod -a
