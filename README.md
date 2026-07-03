# Pixxel Embedded Linux Assignment

A virtual ARM64 platform device, end to end: a Linux kernel driver simulating its registers, two
NASA cFS applications exercising the driver over the Software Bus, a Yocto BSP layer to build a
custom image around it, and a QEMU boot that proves the whole stack works together.

## Architecture

```
qemuarm64.dtb (provided)
        │  pixxel-virt-dev @ 0x60000000, compatible = "pixxel,virt-dev"
        ▼
pixxel_platform_driver.ko
        │  matches the DT node, exposes /dev/pixxel, simulates the Enable/Status registers
        ▼
/dev/pixxel  (character device)
        │
        ▼
pixxel_controller  ◄──Software Bus──►  pixxel_main
  only component that opens              orchestrates the test: enable → wait 50ms →
  /dev/pixxel                            check status → pass/fail
        │
        ▼
meta-pixxel (Yocto BSP layer, MACHINE=qemuarm64)
        │
        ▼
QEMU (runqemu, provided qemuarm64.dtb)
```

`pixxel_main` and `pixxel_controller` communicate exclusively over the cFS Software Bus —
`pixxel_main` never touches the driver directly.

## Register behaviour

| Register | Address    | Direction | Behaviour |
|----------|------------|-----------|-----------|
| Enable   | 0x60000000 | Write     | Write `1` to enable, `0` to disable |
| Status   | 0x60000004 | Read      | Mirrors Enable ~50 ms after a write |

Neither register is backed by real hardware — both are simulated entirely in the driver. The
50 ms propagation delay uses a kernel `delayed_work` item, not `msleep()`, so a `write()` to
`/dev/pixxel` returns immediately instead of blocking the caller for 50 ms.

## Repository layout

```
driver/                 Kernel platform driver (source + Makefile)
cfs-apps/
  pixxel_main/           cFS app: orchestrates the enable → wait → status-check sequence
  pixxel_controller/     cFS app: the only component that opens /dev/pixxel
  mission/               cFE mission config (pixxel_defs/, startup script, build.sh)
meta-pixxel/             Yocto BSP layer — recipes for the driver and the cFS apps
qemuarm64 (1).dtb        Provided device tree binary (unmodified)
run_qemu.sh              One-command QEMU launcher
01_design_document.md    Full architecture and implementation walkthrough
02_yocto_build_guide.md  Yocto/BitBake build notes and troubleshooting reference
03_emulation_output.log  Captured QEMU session showing the passing test run
04_writeup.docx          One-page implementation & challenges summary
```

`cfs-apps/mission/{cfe,osal,psp,tools}/` (NASA's cFE, OSAL, PSP, and elf2cfetbl) are not vendored
here — `build.sh` clones them at build time.

## Building and running

### Native (driver + cFS apps, no QEMU)

```bash
cd driver && make && modinfo pixxel_platform_driver.ko

cd cfs-apps/mission && bash build.sh
cp startup/cfe_es_startup.scr build/native/install/cpu1/cf/
cd build/native/install/cpu1 && ./core-cpu1
```

On the host (no driver loaded) this correctly prints an error from `pixxel_controller` (driver
not present) followed by `pixxel_main` aborting — expected, since the point of this step is
proving the SB wiring and startup ordering work before the driver is in the picture.

### Yocto image + QEMU

```bash
cd poky && source oe-init-build-env build
bitbake core-image-minimal   # meta-pixxel must be in bblayers.conf, MACHINE=qemuarm64
```

Then boot it:

```bash
./run_qemu.sh
```

Inside the guest, the driver has already auto-probed at boot (visible in the kernel log via
udev's device-tree modalias matching — no manual `insmod` needed):

```bash
ls /dev/pixxel
cd /opt/pixxel/cpu1 && ./core-cpu1
```

## Result

```
[ 20.20] pixxel_platform_driver: enable register ← 1 (status update in 50 ms)
[ 20.26] pixxel_platform_driver: status register updated → 1
PIXXEL_MAIN: [PASS] device is ENABLED (status=1)
```

Full transcript: [`03_emulation_output.log`](03_emulation_output.log).

## Key implementation decisions

- **`delayed_work` instead of `msleep()`** for the 50 ms Status propagation, so `write()` never
  blocks the calling process.
- **`spin_lock_irqsave`, not a mutex**, protecting register state — it's touched from both
  process context (syscalls) and the workqueue callback, where a mutex isn't safe.
- **`devm_ioremap_resource()` is called but never dereferenced** — it satisfies the platform
  driver contract without touching memory that isn't backed by real hardware.
- **Strict Software-Bus-only coupling** between the two cFS apps, matching how flight software
  typically isolates hardware access behind a single subsystem app.

See [`01_design_document.md`](01_design_document.md) for the full write-up of the design and
[`04_writeup.docx`](04_writeup.docx) for the assignment's required one-page summary of
implementation and challenges.
