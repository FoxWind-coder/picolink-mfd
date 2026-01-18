# makefile
picolink_mfd-y := core.o mfd-gpio.o mfd-i2c.o mfd-uart.o
obj-m += picolink_mfd.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean