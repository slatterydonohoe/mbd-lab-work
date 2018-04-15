obj-m := kaudio.o
ccflags-y += -g -DDEBUG
KERNEL_SRC ?= /home/build/work/linux
ARCH ?= arm
CROSS_COMPILE ?= /usr/bin/arm-linux-gnueabihf-
SRC := $(shell pwd)
UPLOAD_PATH ?= /home/root/
ZEDBOARD_IP?=1.2.2.1

MODULE_OBJ:=$(obj-m:.o=.ko)

all: modules

modules:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL_SRC) M=$(SRC)

modules_install:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL_SRC) M=$(SRC) modules_install

modules_upload: modules
	scp $(MODULE_OBJ) root@$(ZEDBOARD_IP):$(UPLOAD_PATH)

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers
