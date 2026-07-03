# targets.cmake — Pixxel mission CPU and app configuration
#
# Placed in pixxel_defs/ so the cFE cmake auto-detects MISSIONCONFIG=pixxel.
#
# cpu1_SYSTEM "native" tells cFS to use the host's default compiler (no
# cross-compile toolchain file) and automatically selects the pc-linux PSP.
#
# The Yocto recipe (meta-pixxel/recipes-apps/pixxel-apps) cross-compiles the
# same mission tree for aarch64 by setting PIXXEL_CROSS_BUILD=1 in the
# environment before invoking cmake, which selects the aarch64-linux-gnu
# toolchain file it generates at build time. This keeps build.sh's native
# host build (used for local testing) on the default "native" path.

SET(MISSION_NAME "pixxel")
SET(SPACECRAFT_ID 0x50)

SET(MISSION_CPUNAMES cpu1)
SET(cpu1_PROCESSORID 1)

if(DEFINED ENV{PIXXEL_CROSS_BUILD})
  # Cross-compiled build (Yocto) — selects toolchain-aarch64-linux-gnu.cmake
  # from this same defs directory.
  SET(cpu1_SYSTEM aarch64-linux-gnu)
else()
  # "native" — skip toolchain file, use host gcc; auto-selects pc-linux PSP
  SET(cpu1_SYSTEM native)
endif()

# Apps built as dynamic modules and loaded at runtime via startup script
SET(cpu1_APPLIST
    pixxel_controller
    pixxel_main
)

# Startup script is copied into the install tree for the cpu1 target
SET(cpu1_FILELIST cfe_es_startup.scr)

# Add the cfs-apps/ directory (one level above this mission dir) to the
# module search path so cFS cmake can find pixxel_controller and pixxel_main.
# MISSION_SOURCE_DIR is the parent of cfe/, which is this mission/ directory.
list(APPEND MISSION_MODULE_SEARCH_PATH "..")
