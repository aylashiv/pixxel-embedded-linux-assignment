#!/bin/bash
# Boots the built Pixxel image in QEMU with the provided device tree.
# Just run:  ./run_qemu.sh
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export DEVICE_TREE="$SCRIPT_DIR/poky/build/qemuarm64.dtb"
cd "$SCRIPT_DIR/poky"
source oe-init-build-env build
exec runqemu qemuarm64 nographic nonetwork
