SUMMARY = "Pixxel virtual platform device driver"
DESCRIPTION = "Out-of-tree kernel module for pixxel,virt-dev platform device"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

inherit module

# Point SRC_URI at the driver source directory relative to the layer root.
# In a real project this would be a git URI; for this assignment we use a
# local path so the recipe works straight from the repository checkout.
SRC_URI = "file://pixxel_platform_driver.c \
           file://Makefile"

S = "${WORKDIR}"

# The module Makefile already uses $(KDIR) — the inherit module class sets
# KERNEL_SRC and KERNEL_BUILD_DIR automatically, so no extra variables needed.

RPROVIDES:${PN} += "kernel-module-pixxel-platform-driver"
