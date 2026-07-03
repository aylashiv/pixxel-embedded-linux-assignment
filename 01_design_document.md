# Pixxel Embedded Linux Assignment — Design Document

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Architecture Diagram](#2-architecture-diagram)
3. [Layer 1 — Device Tree](#3-layer-1--device-tree)
4. [Layer 2 — Linux Platform Driver](#4-layer-2--linux-platform-driver)
5. [Layer 3 — cFS User-Space Applications](#5-layer-3--cfs-user-space-applications)
6. [Layer 4 — Yocto BSP Layer](#6-layer-4--yocto-bsp-layer)
7. [Layer 5 — Build and Container Infrastructure](#7-layer-5--build-and-container-infrastructure)
8. [Inter-Component Wiring: How Everything Chains Together](#8-inter-component-wiring-how-everything-chains-together)
9. [Complete Data Flow Walkthrough](#9-complete-data-flow-walkthrough)
10. [Key Concepts Explained](#10-key-concepts-explained)
11. [File Reference Table](#11-file-reference-table)

---

## 1. System Overview

The goal of this project is to build a complete, vertically integrated embedded Linux software stack for a virtual ARM64 device running inside QEMU. The stack spans every level of the system, from hardware description through kernel space, into user space via a flight-software framework, and up to a cross-compilation and image-generation pipeline.

The virtual device (`pixxel,virt-dev`) simulates two memory-mapped registers:

| Register | Address    | Direction | Behaviour |
|----------|------------|-----------|-----------|
| Enable   | 0x60000000 | Write     | Write 1 to enable, 0 to disable |
| Status   | 0x60000004 | Read      | Mirrors Enable approximately 50 ms after a write |

No real hardware backs these registers. Their state is maintained entirely in software inside the kernel driver. The 50 ms propagation delay is simulated with a kernel deferred-work mechanism.

The delivered software stack has five layers:

```
Layer 5:  Build infrastructure  (Docker + Yocto + build.sh)
Layer 4:  Yocto BSP             (meta-pixxel layer)
Layer 3:  cFS applications      (pixxel_main, pixxel_controller)
Layer 2:  Linux kernel driver   (pixxel_platform_driver.ko)
Layer 1:  Device tree           (qemuarm64.dtb)
```

---

## 2. Architecture Diagram

```
┌────────────────────────────────────────────────────────────────────┐
│  Layer 3 — NASA cFS (User Space)                                   │
│                                                                    │
│   ┌─────────────────┐    Software Bus (SB)    ┌─────────────────┐ │
│   │  pixxel_main    │ ──[PIXXEL_CMD_MID 0x1900]──► pixxel_ctrl  │ │
│   │                 │ ◄──[PIXXEL_TLM_MID 0x0900]── (controller) │ │
│   │  Orchestrates   │                          │                 │ │
│   │  test sequence  │                          │  Opens and      │ │
│   └─────────────────┘                          │  drives         │ │
│                                                │  /dev/pixxel    │ │
│                                                └───────┬─────────┘ │
│                                                  File  │           │
│                                                  I/O   │           │
├────────────────────────────────────────────────────────┼───────────┤
│  Layer 2 — Linux Kernel Module                         │           │
│                                                        ▼           │
│   ┌─────────────────────────────────────────────────────────────┐  │
│   │   pixxel_platform_driver.ko                                 │  │
│   │                                                             │  │
│   │   char device /dev/pixxel                                   │  │
│   │   ┌────────────────┐     ┌────────────────────────────┐    │  │
│   │   │  write("1\n")  │────►│  Enable reg = 1            │    │  │
│   │   │  write("0\n")  │     │  schedule delayed_work(50ms)│   │  │
│   │   └────────────────┘     └────────────┬───────────────┘    │  │
│   │   ┌────────────────┐                  │ 50 ms              │  │
│   │   │   read()       │◄────┐            ▼                    │  │
│   │   │  returns "0\n" │     │  ┌──────────────────────┐      │  │
│   │   │  or "1\n"      │     │  │  Status reg = Enable  │      │  │
│   │   └────────────────┘     └──│  (workqueue callback) │      │  │
│   │                             └──────────────────────┘      │  │
│   └─────────────────────────────────────────────────────────────┘  │
│                                                                    │
├────────────────────────────────────────────────────────────────────┤
│  Layer 1 — Device Tree (qemuarm64.dtb)                             │
│                                                                    │
│   pixxel-virt-dev@60000000 {                                       │
│       compatible = "pixxel,virt-dev";                              │
│       reg = <0x00 0x60000000 0x00 0x1000>;                         │
│   }                                                                │
└────────────────────────────────────────────────────────────────────┘
```

---

## 3. Layer 1 — Device Tree

### File: `qemuarm64 (1).dtb`

**What it is:** A compiled Device Tree Binary. Device trees are the mechanism ARM64 Linux uses to describe hardware topology to the kernel. Unlike x86 machines (where the kernel discovers hardware via PCI/ACPI), ARM64 platforms use a static hardware description that is passed to the kernel at boot time.

**What it contains relevant to this project:**

```dts
pixxel-virt-dev@60000000 {
    compatible = "pixxel,virt-dev";
    reg = <0x00 0x60000000 0x00 0x1000>;
};
```

**Interpreting the fields:**

- `pixxel-virt-dev@60000000` — The node name. The `@60000000` suffix is a convention indicating the device's base address, used to disambiguate multiple devices with the same name.
- `compatible = "pixxel,virt-dev"` — This string is the binding between hardware and driver. The Linux kernel's platform driver subsystem scans loaded drivers for a matching `compatible` string. When the kernel boots and enumerates the device tree, it finds this node and searches for a loaded driver whose `of_match_table` contains `"pixxel,virt-dev"`. When found, it calls that driver's `probe()` function.
- `reg = <0x00 0x60000000 0x00 0x1000>` — Declares a 4-kilobyte memory-mapped I/O region starting at physical address `0x60000000000000` (two 32-bit cells per address/size in this 64-bit format). This tells the kernel how much address space to reserve for the device.

**Concept — Virtual Platform Device:** No real hardware exists at `0x60000000` in the QEMU virtual machine. QEMU does not simulate any peripheral at that address. The device tree node is present only to give the kernel's platform bus something to discover and hand to the driver. All register state is maintained in software inside the driver itself; the declared memory region exists purely to satisfy the kernel's resource-claiming contract.

**Why the DTB is not regenerated:** The DTB was provided as a pre-compiled binary for the assignment. Regenerating it would require either the original DTS source file or decompiling and recompiling the binary, which risks altering other board configuration unrelated to this device.

---

## 4. Layer 2 — Linux Platform Driver

### 4.1 `driver/pixxel_platform_driver.c`

This is the heart of the kernel-space implementation. It is an **out-of-tree loadable kernel module** — it is not part of the mainline kernel source tree and is compiled separately against the running kernel's headers.

#### Driver Registration and the Platform Driver Model

The Linux kernel categorises devices into buses (PCI, USB, I2C, platform, etc.). Platform devices are devices that are directly integrated into the SoC or board and are not discoverable via a self-describing protocol like PCI. They are instead described statically, either via board files (older approach) or via device trees (modern ARM approach).

A platform driver registers itself with the kernel's platform bus subsystem by declaring:

```c
static const struct of_device_id pixxel_of_match[] = {
    { .compatible = "pixxel,virt-dev" },
    { }
};
MODULE_DEVICE_TABLE(of, pixxel_of_match);

static struct platform_driver pixxel_driver = {
    .probe  = pixxel_probe,
    .remove = pixxel_remove,
    .driver = {
        .name           = DRIVER_NAME,
        .of_match_table = pixxel_of_match,
    },
};

module_platform_driver(pixxel_driver);
```

The `module_platform_driver()` macro expands to the standard `module_init`/`module_exit` pair, registering the driver with the platform bus at load time and deregistering it at unload. The kernel's bus matching infrastructure then compares the `compatible` string in the device tree with every registered driver's `of_match_table`. When it finds a match, it calls `pixxel_probe()`.

#### The `pixxel_dev` Structure — Per-Device State

All driver state for one device instance is held in a single heap-allocated structure:

```c
struct pixxel_dev {
    void __iomem          *base;          /* ioremap base — claimed but not accessed */
    struct pixxel_regs     regs;          /* software-simulated register state       */
    spinlock_t             lock;          /* protects regs from concurrent access    */
    struct delayed_work    status_work;   /* fires 50ms after an Enable write        */
    struct cdev            cdev;          /* character device node                   */
    dev_t                  devno;         /* major/minor device number               */
    struct class          *cls;           /* sysfs device class                      */
};
```

Using `devm_kzalloc()` instead of `kmalloc()` ties the allocation's lifetime to the device, so it is automatically freed when the device is removed — even if `probe()` fails partway through.

#### `pixxel_probe()` — What Happens at Module Load

When `insmod pixxel_platform_driver.ko` is run (or the kernel auto-loads the module at boot), the bus matching code calls `pixxel_probe()`:

1. **Allocates** the `pixxel_dev` structure with `devm_kzalloc`.
2. **Initialises** the spinlock and the delayed work item.
3. **Calls `platform_get_resource()`** to retrieve the `reg = <…>` entry from the device tree as a kernel `struct resource`, which describes the physical address range.
4. **Calls `devm_ioremap_resource()`** to map the physical address range into kernel virtual address space and to claim the resource (preventing other drivers from using the same range). Critically, **the returned pointer is stored but never dereferenced** — because no real hardware exists at that address in QEMU, reading or writing through this pointer would produce undefined behaviour. The call satisfies the platform driver contract and marks the resource as owned.
5. **Allocates a character device number** via `alloc_chrdev_region()`, which requests a dynamically assigned major/minor pair from the kernel.
6. **Registers the character device** via `cdev_init()` + `cdev_add()`, linking it to the file operations table.
7. **Creates a device class and node** via `class_create()` + `device_create()`, which triggers udev/mdev to create the `/dev/pixxel` file automatically.

The error path uses a `goto`-based ladder to undo each step in reverse if any step fails — a standard Linux kernel pattern for avoiding resource leaks in probe functions.

**API note — Linux ≥ 6.4/6.11 changes:** `class_create()` dropped its `THIS_MODULE` first argument in Linux 6.4. `platform_driver.remove()` changed its return type from `int` to `void` in Linux 6.11. Both were hit during compilation against the 6.17 host kernel and corrected.

#### The 50 ms Delay — `delayed_work`

The assignment specifies that the Status register mirrors the Enable register approximately 50 ms after a write. The naive implementation would be `msleep(50)` inside `pixxel_write()`. This is wrong for two reasons:

1. `msleep()` blocks the calling thread. In the driver, the calling thread is the user-space process that called `write()`. Blocking it for 50 ms holds up the cFS Software Bus while the command is being processed — unacceptable for a real-time system.
2. `msleep()` in interrupt context is not allowed.

The correct approach is `delayed_work`:

```c
/* In write() — returns immediately */
cancel_delayed_work(&dev->status_work);
schedule_delayed_work(&dev->status_work, msecs_to_jiffies(STATUS_DELAY_MS));

/* 50 ms later, in a kernel workqueue thread */
static void pixxel_status_update(struct work_struct *work) {
    spin_lock_irqsave(&dev->lock, flags);
    dev->regs.status = dev->regs.enable;
    spin_unlock_irqrestore(&dev->lock, flags);
}
```

`write()` returns to user space immediately. The kernel schedules the `pixxel_status_update()` callback to run 50 ms later in a kernel workqueue thread. `cancel_delayed_work()` before each reschedule ensures that rapid successive writes reset the 50 ms window correctly rather than stacking multiple callbacks.

#### Spinlock — `spin_lock_irqsave`

The register state (`pixxel_regs`) is accessed from two contexts:
- **Process context:** `pixxel_write()` and `pixxel_read()` called from user-space syscalls.
- **Softirq context:** `pixxel_status_update()` runs in a workqueue, which can execute in a softirq context on some configurations.

A `mutex` cannot be used in softirq context (it may sleep). A `spin_lock_irqsave` is appropriate: it disables local interrupts and takes the spinlock, making it safe across both contexts. The `_irqsave` variant saves the CPU flags before disabling interrupts and restores them on unlock, which is correct whether or not interrupts were already disabled when the lock was acquired.

#### Character Device File Operations

The `/dev/pixxel` character device presents a simple text-based interface:

**`write(fd, "1", 1)`**
1. Copies the byte from user space via `copy_from_user()`.
2. Parses it with `kstrtou32()` after stripping whitespace.
3. Takes the spinlock and writes to `regs.enable`.
4. Releases the spinlock.
5. Reschedules the 50 ms deferred work.
6. Returns immediately.

**`read(fd, buf, n)`**
1. Returns immediately if `*ppos > 0` (only one read per open position).
2. Takes the spinlock, copies `regs.status` to a local variable, releases the spinlock.
3. Formats the value as `"0\n"` or `"1\n"` using `scnprintf()`.
4. Copies to user space via `copy_to_user()`.

The `lseek(fd, 0, SEEK_SET)` call in `pixxel_controller.c` before each read resets `*ppos` to zero, allowing the same file descriptor to be read multiple times.

#### `pixxel_remove()` — Clean Unload

```c
static void pixxel_remove(struct platform_device *pdev) {
    cancel_delayed_work_sync(&dev->status_work);
    device_destroy(dev->cls, dev->devno);
    class_destroy(dev->cls);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->devno, 1);
}
```

`cancel_delayed_work_sync()` is critical: it cancels any pending work and waits for any currently executing callback to finish before returning. This prevents a use-after-free crash where the workqueue callback tries to access `pixxel_dev` memory that has already been freed by `devm_kzalloc` cleanup.

### 4.2 `driver/Makefile`

The Makefile invokes the kernel's external module build system (`kbuild`). The key variable `KDIR` points to the kernel headers directory — by default the running kernel's build directory at `/lib/modules/$(uname -r)/build`. For cross-compilation (e.g., from x86_64 to ARM64), `ARCH` and `CROSS_COMPILE` are set from outside:

```
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR=/path/to/kernel/source
```

Yocto's `inherit module` recipe class handles setting these variables automatically when cross-compiling inside the Yocto build system.

---

## 5. Layer 3 — cFS User-Space Applications

### 5.1 NASA cFS Overview

NASA's Core Flight System (cFS) is a platform-independent, reusable flight software framework. It provides five core services: Executive Service (ES), Software Bus (SB), Event Service (EVS), Table Service (TBL), and Time Service (TIME). This project uses three of them:

- **ES** — Manages application lifecycle (start, stop, restart on exception).
- **EVS** — Structured event logging with event IDs, severity levels, and filtering.
- **SB** — A publish/subscribe message bus. Applications publish messages to a Message ID (MID); any application that has subscribed to that MID receives the message. Applications never need to know who sends or receives their messages — they interact only with the bus.

#### cFS Build Model

cFS applications are compiled as shared libraries (`.so` on Linux). The cFS executive (`core-cpu1`) loads them at runtime by reading a startup script (`cfe_es_startup.scr`). The operating system abstraction layer (OSAL) uses `dlopen()` with `RTLD_NOW | RTLD_GLOBAL` to load each `.so`. With `RTLD_NOW`, all undefined symbols must be resolvable at load time from the already-loaded `core-cpu1` executable (which exports all cFE API symbols via `--export-dynamic`).

**Key constraint:** OSAL enforces `OS_MAX_FILE_NAME = 20` characters for file names. The `.so` filename must not exceed 20 characters. This is why the controller application's compiled output is named `pixxel_ctrl.so` (15 chars) rather than `pixxel_controller.so` (22 chars, which would fail to load with error code `OS_FS_ERR_NAME_TOO_LONG = -104`).

### 5.2 Message ID Architecture

#### `cfs-apps/pixxel_msgids.h`

The shared message ID header used by both applications. It wraps raw numeric IDs in cFE's type-safe `CFE_SB_MsgId_t`:

```c
#define PIXXEL_CMD_MID  CFE_SB_ValueToMsgId(PIXXEL_CMD_MID_RAW)  /* 0x1900 */
#define PIXXEL_TLM_MID  CFE_SB_ValueToMsgId(PIXXEL_TLM_MID_RAW)  /* 0x0900 */
```

Two command function codes define what action the controller should take:

```c
#define PIXXEL_ENABLE_CC      0   /* command the controller to write "1" to /dev/pixxel */
#define PIXXEL_GET_STATUS_CC  1   /* command the controller to read /dev/pixxel         */
```

#### `cfs-apps/mission/pixxel_defs/cpu1_msgids.h`

The mission-wide numeric ID registry for the cpu1 processor. It defines the raw values and reserves the cFE core service IDs. All application IDs must be unique across the mission. By convention:

- `0x1xxx` range — command messages (sent *to* an application)
- `0x0xxx` range — telemetry messages (sent *from* an application)

The pixxel applications use `0x1900` (commands to controller) and `0x0900` (telemetry from controller).

### 5.3 `pixxel_controller` Application

#### Files
- `pixxel_controller/CMakeLists.txt` — cFS app build definition using `add_cfe_app()`
- `pixxel_controller/fsw/src/pixxel_controller.c` — Application main entry point
- `pixxel_controller/fsw/inc/pixxel_controller_msg.h` — SB message struct definitions
- `pixxel_controller/fsw/inc/pixxel_controller_events.h` — EVS event ID constants

#### Role

`pixxel_controller` is the **only** component in the entire stack that touches `/dev/pixxel`. It acts as a hardware abstraction layer between the cFS Software Bus and the kernel driver. This separation ensures that if the driver interface changes, only the controller needs to be modified; `pixxel_main` remains unchanged.

#### Startup Sequence

```c
void PIXXEL_CTRL_AppMain(void) {
    CFE_EVS_Register(...);             // 1. Register for event logging

    dev_fd = open("/dev/pixxel", O_RDWR);  // 2. Open driver interface
    if (dev_fd < 0) { /* fatal exit */ }

    CFE_SB_CreatePipe(&cmd_pipe, 10, "PIXXEL_CTRL_PIPE");  // 3. Create SB receive pipe
    CFE_SB_Subscribe(PIXXEL_CMD_MID, cmd_pipe);             // 4. Subscribe to commands

    CFE_ES_WaitForStartupSync(0);      // 5. Signal ready; do NOT block
    /* ... command dispatch loop ... */
}
```

`WaitForStartupSync(0)` is the most critical ordering decision. Passing `0` means "signal that I am ready, then return immediately." It does **not** wait for other apps. This allows `pixxel_controller` to start its command loop before `pixxel_main` begins sending commands.

#### Command Dispatch Loop

```c
while (CFE_ES_RunLoop(&run_status)) {
    CFE_SB_ReceiveBuffer(&buf, cmd_pipe, CFE_SB_PEND_FOREVER);
    CFE_MSG_GetFcnCode(&buf->Msg, &fc);
    switch (fc) {
        case PIXXEL_ENABLE_CC:     pixxel_ctrl_enable_device(dev_fd);     break;
        case PIXXEL_GET_STATUS_CC: s = pixxel_ctrl_read_status(dev_fd); break;
    }
    pixxel_ctrl_send_tlm(fc, s);  // acknowledge every command
}
```

`CFE_SB_PEND_FOREVER` means the controller blocks indefinitely waiting for a command, consuming zero CPU when idle.

#### Enable and Status Operations

```c
/* Enable: write "1" to the driver */
write(fd, "1", 1);

/* Status: rewind and read */
lseek(fd, 0, SEEK_SET);
read(fd, buf, sizeof(buf) - 1);
status = (uint8_t)atoi(buf);
```

`lseek(fd, 0, SEEK_SET)` is necessary because the driver's `read()` checks `*ppos > 0` to serve only one read per open position. Rewinding resets the position counter, allowing repeated reads on a single open file descriptor.

#### Telemetry — `PIXXEL_CTRL_Tlm_t`

Every command is acknowledged via a telemetry message on `PIXXEL_TLM_MID`:

```c
typedef struct {
    CFE_MSG_TelemetryHeader_t TlmHeader;
    uint8_t AckedCmdCode;   /* which command code is being acked */
    uint8_t DeviceStatus;   /* 0 or 1 (meaningful only for GET_STATUS ack) */
    uint8_t Spare[2];       /* alignment padding */
} PIXXEL_CTRL_Tlm_t;
```

The `Spare[2]` fields ensure the struct is 4-byte aligned, which is required for correct CCSDS message serialisation.

### 5.4 `pixxel_main` Application

#### Files
- `pixxel_main/CMakeLists.txt` — cFS app build definition
- `pixxel_main/fsw/src/pixxel_main.c` — Application main entry point
- `pixxel_main/fsw/inc/pixxel_main_events.h` — EVS event ID constants

#### Role

`pixxel_main` is the **orchestrator**. It never touches hardware. It drives the test sequence purely by sending and receiving messages on the Software Bus. This is the canonical cFS pattern: application-layer logic expressed as message sequences, with hardware interaction delegated to a separate controller app.

#### Startup Sequence and Ordering Guarantee

```c
void PIXXEL_MAIN_AppMain(void) {
    CFE_EVS_Register(...);

    CFE_SB_CreatePipe(&tlm_pipe, 10, "PIXXEL_MAIN_PIPE");
    CFE_SB_Subscribe(PIXXEL_TLM_MID, tlm_pipe);  // subscribe BEFORE sending

    CFE_ES_WaitForStartupSync(10000);  // block up to 10s for all apps to be ready
    /* ... test sequence ... */
}
```

`WaitForStartupSync(10000)` blocks until every application in the startup script has called `WaitForStartupSync(0)`. Because `pixxel_controller` calls `WaitForStartupSync(0)` during its init, by the time `pixxel_main` unblocks, the controller's SB pipe already exists and is subscribed to `PIXXEL_CMD_MID`. The 10-second timeout guards against an infinite block if the controller fails to start.

The subscription to `PIXXEL_TLM_MID` is created **before** calling `WaitForStartupSync`. This is essential — if main subscribed after unblocking, there would be a race window where the controller could send a telemetry message before main is listening.

#### Test Sequence

```
1. pixxel_main_send_cmd(PIXXEL_ENABLE_CC)      → publish on PIXXEL_CMD_MID
2. pixxel_main_await_tlm(tlm_pipe, ENABLE_CC) → block on PIXXEL_TLM_MID, 5s timeout
3. OS_TaskDelay(50)                             → wait for Status register to propagate
4. pixxel_main_send_cmd(PIXXEL_GET_STATUS_CC)  → publish on PIXXEL_CMD_MID
5. pixxel_main_await_tlm(tlm_pipe, STATUS_CC) → block on PIXXEL_TLM_MID, 5s timeout
6. if (tlm->DeviceStatus == 1): OS_printf("[PASS]") else OS_printf("[FAIL]")
```

The `OS_TaskDelay(50)` at step 3 mirrors the driver's 50 ms `delayed_work` timer. Without this delay, reading the Status register immediately after enabling would return `0` because the workqueue callback has not yet fired.

### 5.5 Mission Infrastructure

#### `cfs-apps/mission/pixxel_defs/targets.cmake`

This is the mission configuration file that the cFE cmake build system reads to understand what CPUs and apps to build:

```cmake
SET(MISSION_NAME "pixxel")
SET(MISSION_CPUNAMES cpu1)
SET(cpu1_SYSTEM native)       # use host gcc, selects pc-linux PSP automatically
SET(cpu1_APPLIST pixxel_controller pixxel_main)
list(APPEND MISSION_MODULE_SEARCH_PATH "..")  # find apps one level up in cfs-apps/
```

The `pixxel_defs` directory name is significant: cFE cmake auto-detects `MISSIONCONFIG = "pixxel"` by scanning the parent directory for `*_defs` subdirectories. The mission defs directory must match the pattern `<MISSIONCONFIG>_defs`.

#### `cfs-apps/mission/startup/cfe_es_startup.scr`

```
CFE_APP, /cf/pixxel_ctrl.so, PIXXEL_CTRL_AppMain, PIXXEL_CONTROLLER, 70, 16384, 0x0, 0;
CFE_APP, /cf/pixxel_main.so, PIXXEL_MAIN_AppMain, PIXXEL_MAIN,       80, 16384, 0x0, 0;
! End of startup script
```

Field layout: `Object Type | Path | Entry Point | cFE Name | Priority | Stack Size | Load Addr | Exception Action`

Critical rules:
- The first `!` character is treated as end-of-file by the cFE ES parser — all comments must come **after** all `CFE_APP` entries.
- Priority 70 (controller) < 80 (main): lower number = higher scheduling priority. The controller is scheduled before the main app, providing an additional ordering guarantee beyond `WaitForStartupSync`.
- Exception Action `0x0` means "restart the app" on unhandled exception.
- `/cf/` is the OSAL virtual path for the non-volatile storage directory, which maps to `./cf/` relative to the working directory when running on Linux.

#### `cfs-apps/mission/build.sh`

The build orchestration script:

1. Clones `cFE`, `OSAL`, `PSP`, and `elf2cfetbl` from NASA GitHub into the mission directory (skipping if already present).
2. Configures cmake with `-S cfe/` — the cmake source entry point must be the cFE directory, not the mission root. The cFE `CMakeLists.txt` orchestrates the two-level build (mission config + per-CPU arch build).
3. Sets `CMAKE_INSTALL_PREFIX` to a local path to avoid needing root for installation.
4. Builds with target `mission-install`, which compiles everything and copies binaries into `install/cpu1/cf/`.

#### `cfs-apps/mission/CMakeLists.txt`

A minimal shim that is no longer the primary cmake entry point (build.sh uses `-S cfe/` instead). It is retained for reference. The only functional change made was correcting `mission-build.cmake` to `mission_build.cmake` (hyphen vs underscore) to match the actual filename in the cFE source tree.

---

## 6. Layer 4 — Yocto BSP Layer

### 6.1 What Yocto Does

Yocto is a framework for building custom Linux distributions from source. Rather than installing packages onto an existing OS, Yocto fetches and cross-compiles every component — kernel, libc, init system, applications — and assembles them into a filesystem image and bootloader. The result is a minimal, purpose-built Linux image suitable for embedded deployment.

Key Yocto concepts used here:
- **Layers:** Yocto is organised into layers, each providing a set of recipes. The `meta-pixxel` layer adds the pixxel-specific software on top of the baseline `poky` layer.
- **Recipes (`.bb` files):** Instructions for fetching, patching, configuring, compiling, and installing a single software package.
- **`MACHINE`:** Specifies the target hardware. `qemuarm64` is a built-in Yocto machine that targets a 64-bit ARM QEMU virtual machine.
- **`inherit module`:** A Yocto recipe class specifically for out-of-tree kernel modules. It automatically sets `ARCH`, `CROSS_COMPILE`, `KDIR`, and `KBUILD_EXTRA_SYMBOLS` correctly for cross-compilation.

### 6.2 `meta-pixxel/conf/layer.conf`

```bitbake
BBPATH .= ":${LAYERDIR}"
BBFILES += "${LAYERDIR}/recipes-*/*/*.bb"

BBFILE_COLLECTIONS += "pixxel"
BBFILE_PATTERN_pixxel = "^${LAYERDIR}/"
BBFILE_PRIORITY_pixxel = "6"

LAYERDEPENDS_pixxel = "core"
LAYERSERIES_COMPAT_pixxel = "scarthgap styhead walnascar"
```

- `BBPATH` — Adds this layer's directory to BitBake's search path so it can find recipes and configuration files.
- `BBFILES` — A glob pattern that tells BitBake where to find recipe files within this layer. All `.bb` files under `recipes-*/` will be discovered.
- `BBFILE_PRIORITY_pixxel = "6"` — Determines which layer's version of a recipe wins when multiple layers provide the same package. Priority 6 is higher than the default core layer (priority 5), so this layer can override core recipes if needed.
- `LAYERSERIES_COMPAT_pixxel` — Declares compatibility with Yocto release codenames. Scarthgap (5.0 LTS) is the primary target.

### 6.3 `meta-pixxel/recipes-kernel/pixxel-driver/pixxel-driver_1.0.bb`

```bitbake
inherit module

SRC_URI = "file://pixxel_platform_driver.c \
           file://Makefile"

S = "${WORKDIR}"

RPROVIDES:${PN} += "kernel-module-pixxel-platform-driver"
```

- `inherit module` — This single line is the most important part. The `module` class handles everything needed to build an out-of-tree kernel module: it sets the kernel source directory, cross-compilation flags, calls `make` with the kernel build system, and installs the resulting `.ko` file into `/lib/modules/<version>/extra/` in the target image. It also runs `depmod` to update module dependency information.
- `SRC_URI = "file://..."` — Sources the C file and Makefile from the `files/` subdirectory of the recipe directory (symlinked from the project's `driver/` directory).
- `RPROVIDES` — Declares that this package provides the virtual package name `kernel-module-pixxel-platform-driver`, which can be used by other recipes that want to declare a dependency on the driver without hard-coding the package name.

### 6.4 `meta-pixxel/recipes-apps/pixxel-apps/pixxel-apps_1.0.bb`

```bitbake
inherit cmake

SRC_URI = "file://cfs-apps"

do_install() {
    find ${B} -name "pixxel_*.so" -exec install -m 0755 {} ${D}${bindir} \;
    find ${B} -name "pixxel_*" -type f -executable \
         -exec install -m 0755 {} ${D}${bindir} \;
}
```

- `inherit cmake` — Tells Yocto to use the CMake build system. Yocto provides a cross-compilation toolchain file automatically, so the cmake build inherits the correct `CC`, `CFLAGS`, `LDFLAGS` for the target architecture without any manual configuration.
- `do_install` — Custom install step that copies the compiled pixxel shared libraries and executables into the target image's `/usr/bin/` (`${bindir}`).

---

## 7. Layer 5 — Build and Container Infrastructure

### 7.1 `docker/Dockerfile`

The Dockerfile creates a reproducible, self-contained Yocto build environment based on Ubuntu 22.04. It pre-installs all Yocto Scarthgap dependencies (including QEMU, Python, and cross-compilation toolchains), creates a non-root `yoctobuilder` user with a UID/GID matching the host user (so that volume-mounted files have correct ownership), and sets the locale to `en_US.UTF-8` (required by Yocto).

The image does not contain `poky` or any build trees — those are mounted from the host at runtime, allowing the image to remain small (~1 GB) while the build tree (which can reach 50-90 GB) lives on external storage.

### 7.2 `docker/run.sh`

Builds the Docker image (cached after first run) and launches the container with:
- Project source directory mounted read-only at `/workdir/pixxel`
- Build output directory (pendrive or freed NVMe) mounted at `/workdir/build-output`
- The provided `qemuarm64.dtb` mounted at `/workdir/qemuarm64.dtb`
- KVM device passed through for hardware-accelerated QEMU (if available)
- Host networking for fetching sources

The script validates that the build directory exists and is on a filesystem that supports symlinks (ext4 or xfs) — Yocto makes extensive use of symlinks and will fail on FAT32.

---

## 8. Inter-Component Wiring: How Everything Chains Together

### 8.1 The Device Tree → Driver Binding Chain

```
qemuarm64.dtb compiled binary
    │  contains: compatible = "pixxel,virt-dev"
    │             reg = <0x00 0x60000000 0x00 0x1000>
    ▼
QEMU boots Linux with "-dtb qemuarm64.dtb"
    │  kernel parses DTB, discovers pixxel-virt-dev@60000000 node
    ▼
Kernel platform bus subsystem
    │  scans loaded modules for driver with matching of_device_id
    ▼
pixxel_platform_driver.ko  (loaded via insmod or Yocto auto-load)
    │  pixxel_probe() called with resource: MEM @ 0x60000000, size 0x1000
    ▼
probe() creates /dev/pixxel
    │  char device number allocated, cdev registered, device_create()
    ▼
udev/mdev creates /dev/pixxel node in filesystem
```

### 8.2 The Driver → Controller Binding

```
/dev/pixxel character device node
    │  PIXXEL_CTRL_AppMain() called at cFS startup
    ▼
pixxel_controller opens /dev/pixxel with O_RDWR
    │  file descriptor retained for lifetime of the app
    ▼
write(fd, "1", 1)  ──────────────────────►  pixxel_write() in driver
                                             sets regs.enable = 1
                                             schedules delayed_work(50ms)
                                             returns immediately

[50ms later in workqueue]                   pixxel_status_update()
                                             sets regs.status = regs.enable

lseek(fd, 0, SEEK_SET)  ─────────────────►  resets file position
read(fd, buf, 4)  ───────────────────────►  pixxel_read() in driver
                  ◄──────────────────────    returns "1\n" (status = 1)
```

### 8.3 The Controller → Main SB Chain

```
pixxel_main                           pixxel_controller
    │                                       │
    │  subscribes PIXXEL_TLM_MID 0x0900    │  subscribes PIXXEL_CMD_MID 0x1900
    │                                       │
    │  CFE_ES_WaitForStartupSync(10000)     │  CFE_ES_WaitForStartupSync(0)
    │  [blocks until controller ready]      │  [signals ready, returns now]
    │                                       │
    ◄──────── startup sync completes ───────┘
    │
    │  CFE_SB_TransmitMsg(PIXXEL_CMD_MID, CC=ENABLE)
    │  ──────────────────────────────────────────────────────►
    │                                      CFE_SB_ReceiveBuffer() unblocks
    │                                      write(dev_fd, "1", 1)
    │                                      CFE_SB_TransmitMsg(PIXXEL_TLM_MID, ack)
    │  ◄──────────────────────────────────────────────────────
    │  CFE_SB_ReceiveBuffer() unblocks
    │
    │  OS_TaskDelay(50ms)   ←── waits for Status register to propagate
    │
    │  CFE_SB_TransmitMsg(PIXXEL_CMD_MID, CC=GET_STATUS)
    │  ──────────────────────────────────────────────────────►
    │                                      lseek(dev_fd, 0, SEEK_SET)
    │                                      read(dev_fd, buf)   → "1\n"
    │                                      CFE_SB_TransmitMsg(PIXXEL_TLM_MID, status=1)
    │  ◄──────────────────────────────────────────────────────
    │  tlm->DeviceStatus == 1
    │
    │  OS_printf("PIXXEL_MAIN: [PASS] device is ENABLED")
    ▼
    CFE_ES_ExitApp()
```

### 8.4 The CMake → cFS Build Chain

```
build.sh
    │  git clone cFE, OSAL, PSP, elf2cfetbl
    │
    │  cmake -S cfe/ -B build/native/
    ▼
cfe/CMakeLists.txt  (the cFE cmake entry point)
    │  includes cmake/mission_build.cmake
    │  calls initialize_globals()
    │    → scans parent dir for *_defs: finds pixxel_defs/ → MISSIONCONFIG=pixxel
    │  includes cmake/mission_defaults.cmake   (core module list)
    │  includes pixxel_defs/targets.cmake      (our CPU/app config)
    │  calls read_targetconfig()
    │    → MISSION_CPUNAMES=cpu1, cpu1_SYSTEM=native, cpu1_APPLIST=[pixxel_controller, pixxel_main]
    │  calls prepare()
    │    → searches MISSION_MODULE_SEARCH_PATH (includes "..")
    │    → finds pixxel_controller/ and pixxel_main/ in cfs-apps/
    │  calls process_arch(native_default_cpu1)
    │    → spawns sub-cmake for native CPU build
    ▼
build/native/native/default_cpu1/  (arch-specific sub-build)
    │  uses arch_build.cmake
    │  detects Linux → CFE_SYSTEM_PSPNAME = "pc-linux"
    │  builds: cFE core, OSAL, PSP, pixxel_ctrl.so, pixxel_main.so
    ▼
cmake --build --target mission-install
    │  copies pixxel_ctrl.so, pixxel_main.so → install/cpu1/cf/
    │  copies core-cpu1 → install/cpu1/
    ▼
runtime: cd install/cpu1 && ./core-cpu1
    │  reads cf/cfe_es_startup.scr
    │  dlopen("./cf/pixxel_ctrl.so")   → PIXXEL_CTRL_AppMain
    │  dlopen("./cf/pixxel_main.so")   → PIXXEL_MAIN_AppMain
```

### 8.5 The Yocto → QEMU Chain

```
meta-pixxel layer  +  poky (baseline)
    │
    │  bitbake core-image-minimal
    ▼
Yocto build system
    │  cross-compiles kernel for qemuarm64
    │  builds pixxel_platform_driver.ko via "inherit module"
    │  builds pixxel_ctrl.so + pixxel_main.so via "inherit cmake"
    │  packages everything into rootfs image
    ▼
runqemu qemuarm64 nographic dtb=/path/to/qemuarm64.dtb
    │  QEMU launches with:
    │    machine = virt (generic ARM64 virtual machine)
    │    dtb = qemuarm64.dtb  (our device tree, includes pixxel node)
    │    kernel = from Yocto build
    │    rootfs = from Yocto build
    ▼
Linux kernel boots inside QEMU
    │  kernel parses DTB, discovers pixxel-virt-dev@60000000
    │  loads pixxel_platform_driver.ko  (auto-loaded via depmod/udev)
    │  /dev/pixxel appears
    ▼
cFE starts (via init script or manual)
    │  pixxel_ctrl opens /dev/pixxel
    │  pixxel_main runs test sequence
    ▼
Console output:
    PIXXEL_MAIN: [PASS] device is ENABLED (status=1)
```

---

## 9. Complete Data Flow Walkthrough

The following describes a full successful execution, from kernel boot to PASS result, tracing every byte of data through every layer.

### Step 1 — Kernel boots, driver probes

QEMU loads the device tree. The kernel's platform bus subsystem walks the DTB, finds `compatible = "pixxel,virt-dev"`, matches it against `pixxel_platform_driver.ko` (already loaded), and calls `pixxel_probe()`.

`pixxel_probe()` allocates `pixxel_dev` (zeroed memory, so `regs.enable = 0`, `regs.status = 0`). It calls `alloc_chrdev_region()` to get (say) major=240, minor=0. It calls `cdev_add()` and `device_create()`. udev creates `/dev/pixxel`.

State after this step: `/dev/pixxel` exists. Both simulated registers are `0`. No timer is running.

### Step 2 — cFE starts, apps load

`core-cpu1` starts. ES reads `cfe_es_startup.scr`. It calls `dlopen("./cf/pixxel_ctrl.so", RTLD_NOW|RTLD_GLOBAL)`. The OSAL loader successfully opens the `.so` (the filename `pixxel_ctrl.so` is 15 characters, within the 20-character limit).

`PIXXEL_CTRL_AppMain()` runs:
- Calls `open("/dev/pixxel", O_RDWR)`. The VFS layer looks up the inode for `/dev/pixxel`, finds the registered `pixxel_fops`, calls `pixxel_open()`. A file descriptor `dev_fd` is returned.
- Creates SB pipe, subscribes to `PIXXEL_CMD_MID (0x1900)`.
- Calls `CFE_ES_WaitForStartupSync(0)` — signals "I am ready" and returns.
- Blocks on `CFE_SB_ReceiveBuffer(…, CFE_SB_PEND_FOREVER)`.

`PIXXEL_MAIN_AppMain()` runs concurrently:
- Creates SB pipe, subscribes to `PIXXEL_TLM_MID (0x0900)`.
- Calls `CFE_ES_WaitForStartupSync(10000)` — blocks until controller signals ready.
- Controller already signalled, so main unblocks immediately.

### Step 3 — ENABLE command

`pixxel_main` calls `CFE_SB_TransmitMsg()` with a message on `PIXXEL_CMD_MID (0x1900)` with function code `PIXXEL_ENABLE_CC (0)`.

The SB delivers it to the controller's pipe. The controller unblocks from `CFE_SB_ReceiveBuffer()`.

`pixxel_ctrl_enable_device(dev_fd)` is called:
- `write(dev_fd, "1", 1)` triggers a `write()` syscall.
- The VFS calls `pixxel_write()` with buffer `"1"` and count `1`.
- `kstrtou32()` parses the value `1`.
- Spinlock taken. `regs.enable = 1`. Spinlock released.
- `cancel_delayed_work()` clears any pending timer (there is none yet).
- `schedule_delayed_work(&dev->status_work, msecs_to_jiffies(50))` schedules the callback 50 ms from now.
- `write()` returns `1` (bytes written).

`pixxel_ctrl_send_tlm(PIXXEL_ENABLE_CC, 0)` publishes on `PIXXEL_TLM_MID (0x0900)`.

`pixxel_main` unblocks from `CFE_SB_ReceiveBuffer()`, verifies `tlm->AckedCmdCode == PIXXEL_ENABLE_CC`.

### Step 4 — 50 ms wait

`pixxel_main` calls `OS_TaskDelay(50)`. The OSAL sleeps the calling thread for 50 ms.

Simultaneously, in the kernel, the workqueue timer fires. `pixxel_status_update()` is called in a kernel worker thread:
- Spinlock taken. `regs.status = regs.enable` → `regs.status = 1`. Spinlock released.

### Step 5 — GET_STATUS command

`pixxel_main` sends another message on `PIXXEL_CMD_MID` with `PIXXEL_GET_STATUS_CC (1)`.

Controller calls `pixxel_ctrl_read_status(dev_fd)`:
- `lseek(dev_fd, 0, SEEK_SET)` — syscall → VFS → driver's `llseek` (default `generic_file_llseek`) resets file position to 0.
- `read(dev_fd, buf, 3)` — syscall → VFS → `pixxel_read()`.
  - `*ppos == 0`, so read is permitted.
  - Spinlock taken. `status = regs.status = 1`. Spinlock released.
  - `scnprintf(kbuf, 4, "%u\n", 1)` → `kbuf = "1\n"`, len = 2.
  - `copy_to_user(ubuf, kbuf, 2)` copies `"1\n"` to the controller's userspace buffer.
  - `*ppos += 2`.
  - Returns 2.
- `atoi("1\n")` returns `1`. `status = 1`.

`pixxel_ctrl_send_tlm(PIXXEL_GET_STATUS_CC, 1)` publishes telemetry with `DeviceStatus = 1`.

### Step 6 — PASS verdict

`pixxel_main` receives telemetry. `tlm->AckedCmdCode == PIXXEL_GET_STATUS_CC`. `tlm->DeviceStatus == 1`.

```c
OS_printf("PIXXEL_MAIN: [PASS] device is ENABLED (status=1)\n");
CFE_EVS_SendEvent(PIXXEL_MAIN_STATUS_OK_INF_EID, INFO,
                  "PIXXEL_MAIN: [PASS] device status = 1 — device is ENABLED");
```

Both log outputs appear in the console. `CFE_ES_ExitApp()` is called. The application exits cleanly.

---

## 10. Key Concepts Explained

### Device Tree and Compatible Matching

The `compatible` property is a list of strings (most-specific to most-generic). The Linux kernel's `of_match_device()` function walks the driver's `of_match_table` looking for any string that matches any string in the node's `compatible` property list. This is the primary discovery mechanism for ARM64 platform devices. There is no runtime bus scan — the driver must be loaded before the kernel attempts to probe.

### MMIO Resource Claiming Without Dereferencing

`devm_ioremap_resource()` does two things: it calls `request_mem_region()` to register the physical address range as owned by this driver (visible in `/proc/iomem`), and it maps the range into kernel virtual address space via `ioremap()`. For a real device, you would read/write device registers through the returned pointer. For this virtual device, dereferencing the pointer would access unmapped QEMU memory, causing a bus error. The pointer is stored to satisfy the API contract but is never used.

### Kernel Spinlocks vs Mutexes

A mutex in the Linux kernel is a sleeping lock — a thread that cannot acquire a mutex is put to sleep and descheduled. This is fine in process context (a syscall) but not in interrupt context or softirq context, where sleeping is not allowed. A spinlock is a busy-wait lock — the thread keeps polling the lock in a tight loop. It is safe in all contexts. `spin_lock_irqsave()` additionally disables local interrupts on the CPU, preventing an interrupt handler from trying to take the same lock and deadlocking.

### `RTLD_NOW` vs `RTLD_LAZY` and `OS_MAX_FILE_NAME`

OSAL uses `dlopen(path, RTLD_NOW | RTLD_GLOBAL)`. `RTLD_NOW` requires all symbol references in the `.so` to be resolved at load time. `RTLD_GLOBAL` makes the symbols exported by the `.so` available to subsequently loaded modules. The undefined cFE API symbols in each `.so` are resolved from `core-cpu1` which is linked with `-rdynamic`, exporting all its symbols to the dynamic linker's global namespace. The `OS_MAX_FILE_NAME = 20` limit is enforced in OSAL's path translation before `dlopen` is even called.

### cFS Software Bus — Publish/Subscribe

The SB is an in-process, zero-copy message bus. `CFE_SB_TransmitMsg()` does not send a message to a specific destination — it publishes to a Message ID. Any application that has called `CFE_SB_Subscribe(MID, pipe)` will receive a copy in its pipe. Applications are completely decoupled: `pixxel_main` does not know `pixxel_controller` exists; it only knows about message IDs. This design makes it trivial to add a logging app, a ground-station relay, or a watchdog that also subscribes to `PIXXEL_TLM_MID` without changing any existing code.

### `WaitForStartupSync` — Startup Ordering Without Race Conditions

cFE apps start in arbitrary order. `CFE_ES_WaitForStartupSync(timeout_ms)` is the synchronisation barrier:
- An app calling it with `timeout > 0` blocks until all apps have reached the barrier.
- An app calling it with `0` signals "I've reached the barrier" and returns immediately.

In this design, the controller signals first (`WaitForStartupSync(0)`) and enters its command loop. The main app then unblocks (`WaitForStartupSync(10000)`) knowing the controller's SB pipe exists and is subscribed. Without this, `pixxel_main` could send a command before `pixxel_controller` has subscribed, and the message would be dropped silently.

### Yocto `inherit module`

The `module` BitBake class abstracts the entire complexity of cross-compiling kernel modules. It automatically:
1. Declares a build dependency on the target kernel (`DEPENDS += "virtual/kernel"`).
2. Sets `ARCH`, `CROSS_COMPILE`, and `KDIR` from the Yocto cross-compilation toolchain.
3. Calls the kernel's external module build system (`make -C $KDIR M=$S`).
4. Stages the `.ko` file to `/lib/modules/<kernel-version>/extra/`.
5. Runs `depmod` post-installation so the module can be loaded by name with `modprobe`.

---

## 11. File Reference Table

| File | Layer | Purpose |
|------|-------|---------|
| `qemuarm64 (1).dtb` | 1 — Device Tree | Pre-compiled DTB describing the virtual board, including the `pixxel,virt-dev` node |
| `driver/pixxel_platform_driver.c` | 2 — Kernel | Platform driver implementing /dev/pixxel char device with simulated registers and 50 ms delayed_work |
| `driver/Makefile` | 2 — Kernel | kbuild Makefile for native and cross-compilation of the driver |
| `driver/pixxel_platform_driver.ko` | 2 — Kernel | Compiled kernel module (native x86_64 build artifact) |
| `cfs-apps/pixxel_msgids.h` | 3 — cFS | Shared SB message ID and function code definitions for both apps |
| `cfs-apps/pixxel_controller/CMakeLists.txt` | 3 — cFS | cFS cmake build definition; `add_cfe_app(pixxel_ctrl ...)` produces `pixxel_ctrl.so` |
| `cfs-apps/pixxel_controller/fsw/src/pixxel_controller.c` | 3 — cFS | Controller app: opens `/dev/pixxel`, dispatches ENABLE/GET_STATUS commands |
| `cfs-apps/pixxel_controller/fsw/inc/pixxel_controller_msg.h` | 3 — cFS | SB message struct definitions (command and telemetry) |
| `cfs-apps/pixxel_controller/fsw/inc/pixxel_controller_events.h` | 3 — cFS | EVS event ID constants for the controller |
| `cfs-apps/pixxel_main/CMakeLists.txt` | 3 — cFS | cFS cmake build definition for `pixxel_main.so` |
| `cfs-apps/pixxel_main/fsw/src/pixxel_main.c` | 3 — cFS | Main app: orchestrates ENABLE→wait→GET_STATUS→PASS/FAIL sequence |
| `cfs-apps/pixxel_main/fsw/inc/pixxel_main_events.h` | 3 — cFS | EVS event ID constants for main |
| `cfs-apps/mission/pixxel_defs/targets.cmake` | 3 — cFS | Mission CPU/app config; auto-detected as MISSIONCONFIG=pixxel |
| `cfs-apps/mission/pixxel_defs/cpu1_msgids.h` | 3 — cFS | Mission-wide numeric SB message ID registry |
| `cfs-apps/mission/pixxel_defs/cfe_perfids.h` | 3 — cFS | cFE performance monitor IDs (required by cFE core; copied from sample_defs) |
| `cfs-apps/mission/pixxel_defs/native_osconfig.cmake` | 3 — cFS | OSAL config for native builds (enables PERMISSIVE mode for non-root execution) |
| `cfs-apps/mission/pixxel_defs/native_pspconfig.cmake` | 3 — cFS | PSP config for native builds |
| `cfs-apps/mission/pixxel_defs/default_osconfig.cmake` | 3 — cFS | Default OSAL config (applies to all builds) |
| `cfs-apps/mission/pixxel_defs/default_pspconfig.cmake` | 3 — cFS | Default PSP config (applies to all builds) |
| `cfs-apps/mission/startup/cfe_es_startup.scr` | 3 — cFS | App startup script; comments must appear after CFE_APP entries (first `!` = EOF) |
| `cfs-apps/mission/build.sh` | 3 — cFS | Clones dependencies, configures cmake with `-S cfe/`, builds and installs |
| `cfs-apps/mission/CMakeLists.txt` | 3 — cFS | Legacy mission cmake shim (retained; no longer the cmake entry point) |
| `meta-pixxel/conf/layer.conf` | 4 — Yocto | Layer registration; declares compatibility with scarthgap/styhead/walnascar |
| `meta-pixxel/recipes-kernel/pixxel-driver/pixxel-driver_1.0.bb` | 4 — Yocto | `inherit module` recipe for cross-compiling the kernel module |
| `meta-pixxel/recipes-apps/pixxel-apps/pixxel-apps_1.0.bb` | 4 — Yocto | `inherit cmake` recipe for cross-compiling the cFS apps |
| `docker/Dockerfile` | 5 — Build | Ubuntu 22.04 image with all Yocto deps; non-root user for Yocto compliance |
| `docker/run.sh` | 5 — Build | Builds Docker image, mounts source and external storage, launches container |
