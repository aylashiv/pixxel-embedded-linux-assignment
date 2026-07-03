#!/bin/bash
# build.sh — fetch cFS dependencies and build the pixxel mission
#
# Run from: cfs-apps/mission/
# Requires: cmake, gcc (or aarch64 cross-toolchain for ARM target)

set -e
MISSION_DIR="$(cd "$(dirname "$0")" && pwd)"

# ---------------------------------------------------------------------------
# 1. Clone cFS component repositories (skip if already present)
# ---------------------------------------------------------------------------
clone_if_absent() {
    local dir="$1" url="$2" branch="$3"
    if [ ! -d "$dir/.git" ]; then
        echo "--- Cloning $url (branch: $branch) ---"
        git clone --depth 1 --branch "$branch" "$url" "$dir"
    else
        echo "--- $dir already present, skipping clone ---"
    fi
}

cd "$MISSION_DIR"

clone_if_absent cfe       https://github.com/nasa/cFE.git        main
clone_if_absent osal      https://github.com/nasa/osal.git       main
clone_if_absent psp       https://github.com/nasa/PSP.git        main
clone_if_absent tools     https://github.com/nasa/elf2cfetbl.git main

# ---------------------------------------------------------------------------
# 2. Configure (native host build for local testing)
# ---------------------------------------------------------------------------
BUILD_DIR="$MISSION_DIR/build/native"
mkdir -p "$BUILD_DIR"

# cFS cmake must be pointed at cfe/ (the cFE source), not the mission root.
# MISSIONCONFIG is auto-detected from pixxel_defs/ in the mission directory.
# cpu1_SYSTEM=native in targets.cmake selects the host compiler + pc-linux PSP.
INSTALL_DIR="$BUILD_DIR/install"
cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -S "$MISSION_DIR/cfe" \
    -B "$BUILD_DIR"

# ---------------------------------------------------------------------------
# 3. Build — mission-install copies apps + startup script into install tree
# ---------------------------------------------------------------------------
cmake --build "$BUILD_DIR" --target mission-install --parallel "$(nproc)"

echo ""
echo "=== Build complete ==="
echo "Binaries:       $INSTALL_DIR/cpu1/cf/"
echo "Startup script: $MISSION_DIR/startup/cfe_es_startup.scr"
echo ""
echo "To run (native test):"
echo "  cp startup/cfe_es_startup.scr $INSTALL_DIR/cpu1/cf/"
echo "  cd $INSTALL_DIR/cpu1 && ./core-cpu1"

# ---------------------------------------------------------------------------
# Cross-compile for qemuarm64 (uncomment and set TOOLCHAIN_FILE):
# ---------------------------------------------------------------------------
# BUILD_CROSS="$MISSION_DIR/build/qemuarm64"
# mkdir -p "$BUILD_CROSS"
# cmake \
#     -DCMAKE_TOOLCHAIN_FILE="$MISSION_DIR/pixxel_defs/toolchain-aarch64-linux-gnu.cmake" \
#     -DCMAKE_BUILD_TYPE=Release \
#     -S "$MISSION_DIR/cfe" \
#     -B "$BUILD_CROSS"
# cmake --build "$BUILD_CROSS" --target mission-install --parallel "$(nproc)"
