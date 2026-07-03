#!/bin/bash
# run.sh — build the Docker image (once) and launch the Yocto build environment
#
# Usage:
#   ./run.sh                    # interactive shell inside the container
#   ./run.sh bitbake <target>   # run a bitbake command directly
#
# Prerequisites:
#   1. Pendrive (≥64 GB, ext4) mounted at /mnt/yocto
#      OR enough free space on NVMe (set YOCTO_BUILD_DIR below to a local path)
#   2. Docker running:  sudo systemctl start docker

set -e

# ── Configuration ─────────────────────────────────────────────────────────

# Where the large build artifacts live (pendrive mount or local NVMe path)
YOCTO_BUILD_DIR="${YOCTO_BUILD_DIR:-/mnt/yocto}"

# Your project source directory (mounted read-write so edits persist)
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# Docker image name
IMAGE="pixxel-yocto:latest"

# ── Sanity checks ─────────────────────────────────────────────────────────

if [ ! -d "$YOCTO_BUILD_DIR" ]; then
    echo "ERROR: build directory '$YOCTO_BUILD_DIR' does not exist."
    echo "  If using a pendrive: sudo mount /dev/sdX1 /mnt/yocto"
    echo "  If using NVMe:       export YOCTO_BUILD_DIR=~/yocto-build && mkdir -p \$YOCTO_BUILD_DIR"
    exit 1
fi

# Check the filesystem — Yocto needs symlink support (ext4/xfs, not FAT32)
FS_TYPE=$(stat -f -c %T "$YOCTO_BUILD_DIR" 2>/dev/null || df -T "$YOCTO_BUILD_DIR" | awk 'NR==2{print $2}')
if echo "$FS_TYPE" | grep -qiE "fat|vfat|msdos|exfat"; then
    echo "ERROR: $YOCTO_BUILD_DIR is on a FAT filesystem ($FS_TYPE)."
    echo "Yocto requires symlink support. Format the drive as ext4 first:"
    echo "  sudo mkfs.ext4 -L yocto-build /dev/sdX1"
    exit 1
fi

# ── Build the Docker image (cached after first run) ───────────────────────

echo "--- Building Docker image (skipped if already up to date) ---"
docker build \
    --build-arg HOST_UID="$(id -u)" \
    --build-arg HOST_GID="$(id -g)" \
    -t "$IMAGE" \
    "$(dirname "$0")"

# ── Launch the container ──────────────────────────────────────────────────

echo "--- Starting Yocto build container ---"
echo "    Project source : $PROJECT_DIR  → /workdir/pixxel"
echo "    Build output   : $YOCTO_BUILD_DIR  → /workdir/build-output"
echo ""

docker run --rm -it \
    --name pixxel-yocto \
    \
    `# Source code — your meta-pixxel layer, driver, cFS apps` \
    -v "$PROJECT_DIR:/workdir/pixxel:ro" \
    \
    `# Build output — large tree lives here (pendrive or freed NVMe space)` \
    -v "$YOCTO_BUILD_DIR:/workdir/build-output" \
    \
    `# Pass through the provided qemuarm64.dtb` \
    -v "$PROJECT_DIR/qemuarm64 (1).dtb:/workdir/qemuarm64.dtb:ro" \
    \
    `# Allow QEMU KVM acceleration if available` \
    $([ -e /dev/kvm ] && echo "--device /dev/kvm") \
    \
    `# Network (needed for poky clone and fetching packages)` \
    --network host \
    \
    "$IMAGE" \
    "${@:-/bin/bash}"
