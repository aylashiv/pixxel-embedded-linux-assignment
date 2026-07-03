obj-m := pixxel_platform_driver.o

# Native build: uses the running kernel's headers.
# Yocto's `module` bbclass builds out-of-tree modules by invoking this
# Makefile with KERNEL_SRC set to the target kernel's build tree (not KDIR)
# — prefer that when present so the same Makefile serves both the native
# `make` build and the Yocto cross-build without extra recipe-side plumbing.
ifneq ($(KERNEL_SRC),)
KDIR := $(KERNEL_SRC)
else
KDIR ?= /lib/modules/$(shell uname -r)/build
endif

# Cross-compile (set from Yocto SDK or manually):
#   make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR=/path/to/kernel
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Required by Yocto's `module` bbclass do_install, which invokes this target
# (with MODLIB/DEPMOD/INSTALL_MOD_PATH already set on the command line) to
# stage the built .ko under the image's /lib/modules/<kernel-version>/.
modules_install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all modules_install clean
