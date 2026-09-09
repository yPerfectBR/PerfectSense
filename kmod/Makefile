# SPDX-License-Identifier: AGPL-3.0-or-later
# PerfectSense kernel module — Phase 0 stub (does not bind WMI yet).

# Optional on clang-built kernels (e.g. some rolling spins):
#   make LLVM=1
# Target distro for PerfectSense packaging is Arch Linux (stock gcc headers).

obj-m += perfectsense.o
perfectsense-y := src/perfectsense_core.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
