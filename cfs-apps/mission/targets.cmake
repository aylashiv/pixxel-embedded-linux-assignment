# targets.cmake — Pixxel mission CPU and app configuration
#
# Defines one CPU (cpu1) running the Linux OSAL, targeting qemuarm64.
# Both pixxel apps are dynamically loaded by cFE at runtime via the startup script.

SET(MISSION_CPUNAMES cpu1)

# PROCESSORTYPE maps to a PSP (Platform Support Package).
# "linuxv2" uses the cFE Linux PSP — works for QEMU ARM64 with Linux OSAL.
SET(cpu1_PROCESSORTYPE linuxv2)

# APPLIST: apps that are built as shared libraries and listed in the startup script.
# Order here affects build only — runtime order is controlled by cfe_es_startup.scr.
SET(cpu1_APPLIST
    pixxel_controller
    pixxel_main
)

# No static (built-in) apps beyond the cFE core services.
SET(cpu1_STATICAPPLIST)
