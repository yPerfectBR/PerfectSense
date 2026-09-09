# Optional on clang-built kernels:
#   make LLVM=1
# Target packaging distro is Arch Linux (stock gcc headers).

obj-m += perfectsense.o
perfectsense-y := \
	src/perfectsense_core.o \
	src/wmi.o \
	src/sysfs.o \
	src/dmi.o

ccflags-y += -I$(src)/include

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
