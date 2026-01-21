MODULE_NAME := picolink_mfd
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
BUILD_DIR := $(PWD)/build
PROJECT_ROOT := $(shell realpath $(PWD)/..)

SRCS := core.c mfd-gpio.c mfd-i2c.c mfd-uart.c mfd-adc.c
OBJS := $(SRCS:.c=.o)

all:
	@mkdir -p $(BUILD_DIR)
	
	# 1. Генерируем Kbuild файл "на лету" внутри папки build
	@echo "obj-m := $(MODULE_NAME).o" > $(BUILD_DIR)/Kbuild
	@echo "$(MODULE_NAME)-objs := $(OBJS)" >> $(BUILD_DIR)/Kbuild
	@echo "ccflags-y := -I$(PWD) -I$(PROJECT_ROOT) -Wno-error=incompatible-pointer-types" >> $(BUILD_DIR)/Kbuild

	# 2. Создаем симлинки
	@for f in $(SRCS) picolink.h mfd-adc.h; do \
		ln -sf $(PWD)/$$f $(BUILD_DIR)/$$f; \
	done

	# 3. Запускаем сборку
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) clean
	rm -rf $(BUILD_DIR)