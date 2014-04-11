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

dkms-install:
	mkdir -p /usr/src/smo8800-1.0
	cp -a smo8800.c Makefile dkms.conf /usr/src/smo8800-1.0/
	dkms remove smo8800/1.0 --all || true
	dkms add smo8800/1.0
	dkms autoinstall smo8800/1.0
