PWD := $(shell pwd)
sinclude $(PWD)/../module_conf
LINUX_SRC = /home/yelsin/software/arm-linux-3.3/linux-3.3-fa
STRIPFLAGS = -g --strip-unneeded
STRIP = $(CROSS_COMPILE)strip
SDK_MODULE_SRC = /home/yelsin/software/arm-linux-3.3/module_2014-11-14
##SDK_MODULE_SRC := $(SDK_SRC)module
KERNELDIR := $(LINUX_SRC)

ifneq ($(KERNELRELEASE),)

include $(srctree)/.config

EXTRA_CFLAGS += -fno-common -DMPFX=$(MODULE) -DEXPORT_SYMTAB -I$(SDK_MODULE_SRC)/include/dswmb

ifeq ($(CONFIG_PLATFORM_GM8136),y)
PLATFORM_OBJ=dswmb_main.o
endif

MODULE := dswmb
$(MODULE)-objs = $(PLATFORM_OBJ) dswmb_comch.o dswmb_key.o

obj-m += $(MODULE).o
obj-m := $(sort $(obj-m))

else

all modules:
	$(MAKE) -C $(KERNELDIR) SDK_MODULE_SRC=$(SDK_MODULE_SRC) M=$(PWD) modules
	@$(STRIP) $(STRIPFLAGS) *.ko

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

endif
