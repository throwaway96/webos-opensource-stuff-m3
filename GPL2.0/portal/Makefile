obj-m += portal.o

#KDIR := ./linux
KDIR := $(BUILDDIR)/work/*-starfish-linux-gnueabi/linux-rockhopper/*/git

GF_TOOL_CHAIN	= $(PALMDIR)/BUILD-*/work/*-starfish-linux-gnueabi/external-lg-toolchain/*/sysroot-destdir/arm-lg115x-linux-gnueabi/bin/arm-lg115x-linux-gnueabi-

default:
	make -C $(KDIR) M=$(PWD) modules ARCH=arm CROSS_COMPILE=$(GF_TOOL_CHAIN)

clean:
	rm -rf *.mod.* .*.cmd *.o modules.order Module.symvers .tmp_versions
