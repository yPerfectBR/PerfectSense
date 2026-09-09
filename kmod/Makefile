# Optional on clang-built kernels:
#   make LLVM=1
# Target packaging distro is Arch Linux (stock gcc headers).

obj-m += perfectsense.o
perfectsense-y := \
	src/perfectsense_core.o \
	src/wmi.o \
	src/sysfs.o \
	src/dmi.o \
	src/profile.o \
	src/platform_profile.o \
	src/fan.o \
	src/attrs.o \
	src/rgb.o \
	src/events.o

ccflags-y += -I$(src)/include

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
