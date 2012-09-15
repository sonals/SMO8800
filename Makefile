#
# Makefile for sensor chip drivers.
#

obj-m += smo8800.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) V=1 modules



