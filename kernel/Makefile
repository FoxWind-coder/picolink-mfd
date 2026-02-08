MODULE_NAME := picolink_mfd
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
BUILD_DIR := $(PWD)/build
PROJECT_ROOT := $(shell realpath $(PWD)/..)

SRCS := core.c mfd-gpio.c mfd-i2c.c mfd-uart.c mfd-adc.c mfd-spi.c mfd-pwm.c
OBJS := $(SRCS:.c=.o)

all:
	@mkdir -p $(BUILD_DIR)
	
	# 1. Generate Kbuild file on-the-fly inside this build directory
	@echo "obj-m := $(MODULE_NAME).o" > $(BUILD_DIR)/Kbuild
	@echo "$(MODULE_NAME)-objs := $(OBJS)" >> $(BUILD_DIR)/Kbuild
	@echo "ccflags-y := -I$(PWD) -I$(PROJECT_ROOT) -Wno-error=incompatible-pointer-types" >> $(BUILD_DIR)/Kbuild

	# 2. Create symbolic links for source files and headers
	@for f in $(SRCS); do \
		ln -sf $(PWD)/$$f $(BUILD_DIR)/$$f; \
	done
	@ln -sf $(PWD)/picolink.h $(BUILD_DIR)/picolink.h
	@ln -sf $(PWD)/mfd-adc.h $(BUILD_DIR)/mfd-adc.h

	# 3. Execute the kernel build system
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) clean
	rm -rf $(BUILD_DIR)