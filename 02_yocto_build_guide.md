# Yocto Build Guide — How It Applies to This Project

This document explains what the Yocto build is actually doing for the Pixxel
assignment: why each piece exists, how our two recipes fit into BitBake's
task model, and how to read/drive the build yourself. It assumes you've
read `01_design_document.md` for the driver/cFS side — this covers only
Layer 5 (the BSP build).

---

## 1. What "the Yocto build" produces

One artifact: a bootable root filesystem image for `MACHINE = "qemuarm64"`
containing:

- A Linux kernel (`linux-yocto`, built by Yocto's standard qemuarm64 BSP —
  we don't touch this)
- Our out-of-tree driver, `pixxel_platform_driver.ko`, installed as a kernel
  module
- The full NASA cFS mission — `core-cpu1` plus `pixxel_ctrl.so` /
  `pixxel_main.so` — cross-compiled for aarch64 and installed at
  `/opt/pixxel/cpu1/`

`runqemu` boots that image under QEMU using the provided
`qemuarm64 (1).dtb`. Everything else Yocto builds (busybox, glibc,
init system, etc.) is just what `core-image-minimal` normally pulls in to
produce a working Linux userspace — none of it is assignment-specific.

---

## 2. The layer: `meta-pixxel/`

Yocto organizes recipes into **layers**. A layer is just a directory with a
`conf/layer.conf` that tells BitBake "look for recipes here." Ours:

```
meta-pixxel/
├── conf/layer.conf                 ← registers the "pixxel" collection
├── recipes-kernel/pixxel-driver/
│   ├── pixxel-driver_1.0.bb
│   └── files/                      ← symlinks into driver/ (see §4)
└── recipes-apps/pixxel-apps/
    ├── pixxel-apps_1.0.bb
    └── files/                      ← symlinks into cfs-apps/ (see §5)
```

`layer.conf` sets `LAYERSERIES_COMPAT_pixxel = "scarthgap styhead walnascar"`
— this must include whatever Poky branch you clone (we use `scarthgap`) or
BitBake refuses to load the layer.

The layer is added to a Yocto build by listing its absolute path in
`poky/build/conf/bblayers.conf`, alongside `core`, `meta-poky`, and
`meta-yocto-bsp` (Poky's own layers). That's the only wiring needed —
BitBake auto-discovers every `.bb` file under `recipes-*/*/*.bb`.

---

## 3. Two recipes, two different build strategies

| Recipe | `inherit` | Why |
|---|---|---|
| `pixxel-driver_1.0.bb` | `module` | Standard out-of-tree kernel module pattern — Yocto's `module` class handles finding the kernel build tree, setting `KERNEL_SRC`, and packaging the `.ko` |
| `pixxel-apps_1.0.bb` | *(none — custom `do_configure`/`do_compile`/`do_install`)* | cFE's own CMake system is not a normal single-project build; it re-invokes CMake as a subprocess per target architecture. This doesn't fit the standard `cmake` bbclass, so the recipe drives CMake directly (see §5) |

### 3.1 `pixxel-driver_1.0.bb`

```bitbake
inherit module
SRC_URI = "file://pixxel_platform_driver.c file://Makefile"
S = "${WORKDIR}"
```

`inherit module` is doing almost all the work here: it depends on
`virtual/kernel` (so the qemuarm64 kernel gets built first), points
`KERNEL_SRC`/`KERNEL_BUILD_DIR` at that kernel's build tree, and calls our
existing `driver/Makefile`. Two things in the Makefile itself *did* need
fixing once this actually ran against the qemuarm64 kernel, both now fixed:

**Bug — `KDIR` vs. `KERNEL_SRC`.** The Makefile only recognized `KDIR`
(defaulting to `/lib/modules/$(shell uname -r)/build`, the *host's* kernel).
Yocto's `module` bbclass passes the target kernel's build tree as
`KERNEL_SRC`, a different variable name — our Makefile silently ignored it
and built against the host's 6.17 headers instead of the target's. Fixed
by preferring `KERNEL_SRC` when set:
```make
ifneq ($(KERNEL_SRC),)
KDIR := $(KERNEL_SRC)
else
KDIR ?= /lib/modules/$(shell uname -r)/build
endif
```

**Bug — kernel-version-dependent `.remove` signature.** Once building
against the *correct* kernel tree, compilation failed:
```
error: initialization of 'int (*)(struct platform_device *)' from
incompatible pointer type 'void (*)(struct platform_device *)'
```
`struct platform_driver.remove` changed from `int (*)()` to `void (*)()`
in Linux 6.11 — and the change is a removal, not a deprecation, so no
single signature compiles on both sides. The host kernel here is 6.17
(new signature), but Yocto's qemuarm64
BSP pins `linux-yocto` to **6.6** (old signature) — the same source file
has to satisfy both. Fixed with a `LINUX_VERSION_CODE` compile-time check
around a shared implementation body, so both kernels build from one
source file (see `driver/pixxel_platform_driver.c`).

**Bug — missing `modules_install` target.** `do_install` failed with
`make: *** No rule to make target 'modules_install'`. The Makefile only
defined `all`/`clean`; the `module` bbclass's `do_install` expects a
`modules_install` target (the standard out-of-tree-module convention,
delegating to the kernel build system's own install logic, which honors
the `MODLIB`/`DEPMOD` variables Yocto passes on the command line). Added:
```make
modules_install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
```

None of these needed `ARCH`/`CROSS_COMPILE` handling in the Makefile
itself — the `module` bbclass exports `CC`/`LD`/`AR`/etc. directly as
command-line variables to `make`, which override the kernel build
system's own compiler selection without the Makefile needing to reference
them explicitly.

### 3.2 `pixxel-apps_1.0.bb`

This one is the interesting/nonstandard part. cFE's `build.sh`-driven
native build works like this:

```bash
cmake -S mission/cfe -B build/native          # configure
cmake --build build/native --target mission-install   # build + install
```

`cmake -S mission/cfe` is not a plain library build — cFE's top-level
`CMakeLists.txt` re-invokes `cmake` as a **subprocess**
(`execute_process()` in `cfe/cmake/mission_build.cmake`) once per target
architecture listed in `cpu1_SYSTEM`, passing a **toolchain file** it looks
up by convention: `${MISSION_DEFS}/toolchain-${cpu1_SYSTEM}.cmake`. That
toolchain file is where `CMAKE_C_COMPILER`, sysroot paths, and two cFE-only
variables (`CFE_SYSTEM_PSPNAME`, `OSAL_SYSTEM_OSTYPE`) get set.

Yocto's `cmake` bbclass generates its *own* toolchain file and passes it
via `-DCMAKE_TOOLCHAIN_FILE=...` on the **outer** `cmake` invocation — but
cFE's outer `CMakeLists.txt` ignores that and does its own toolchain
lookup for the **inner** subprocess build. The two conventions don't talk
to each other, so `inherit cmake` would configure the wrong (outer, no-op)
project and never reach the actual cFE build. That's why the recipe skips
`inherit cmake` and reproduces `build.sh`'s two commands directly in
`do_configure()` / `do_compile()`.

**`do_configure()`** does two things:

1. Generates `mission/pixxel_defs/toolchain-aarch64-linux-gnu.cmake` at
   build time, using Yocto's cross-compiler variables (`${CC}`, `${CXX}`,
   `${CFLAGS}`, `${STAGING_DIR_TARGET}`, ...). This file is *generated*,
   not committed — the compiler paths are only known inside a specific
   Yocto build's sysroot (they contain hashes from `TMPDIR`), so hardcoding
   them wouldn't survive a `bitbake clean` or a different build directory.
2. Sets `PIXXEL_CROSS_BUILD=1` in the environment and runs
   `cmake -S mission/cfe -B build`.

**Why `PIXXEL_CROSS_BUILD`, not a hardcoded arch:** `pixxel_defs/targets.cmake`
is shared between the native test build (`build.sh`, still used for local
testing on the host) and this Yocto cross build. It picks `cpu1_SYSTEM`
based on that env var:

```cmake
if(DEFINED ENV{PIXXEL_CROSS_BUILD})
  SET(cpu1_SYSTEM aarch64-linux-gnu)   # → looks for toolchain-aarch64-linux-gnu.cmake
else()
  SET(cpu1_SYSTEM native)              # → host gcc, no toolchain file (build.sh default)
endif()
```

This keeps `build.sh` completely unaffected — it never sets that variable,
so `cpu1_SYSTEM` stays `native` for local testing exactly as before.

**`do_compile()`** just runs `cmake --build ${S}/build --target
mission-install -- ${PARALLEL_MAKE}` — same target `build.sh` uses, so
parallelism follows Yocto's configured `PARALLEL_MAKE` instead of a
hardcoded `-j`.

**`do_install()`** copies the resulting `build/install/cpu1/` tree
(`core-cpu1`, `cf/pixxel_ctrl.so`, `cf/pixxel_main.so`, and cFE's own
built-in test apps) into `${D}/opt/pixxel/cpu1/`, then copies
`startup/cfe_es_startup.scr` into `cf/` — the same manual copy step
`build.sh`'s instructions call out, just done at packaging time instead of
by hand.

**`INSANE_SKIP:${PN} += "dev-so"`** — Yocto's QA checks assume any
unversioned `.so` file outside a `-dev` package is a packaging mistake
(should be `libfoo.so.1` + a `-dev` symlink). Ours are runtime `dlopen()`
plugins loaded by `cfe_es_startup.scr`, not linkable libraries, so that
check is a false positive here and is explicitly silenced.

### 3.2.1 Two real bugs the first build run surfaced

The design in §3.2 above is correct in principle, but two mistakes only
showed up once the recipe actually ran end to end — worth recording since
both are generic "outer build vs. inner cross sub-build" traps, not
Pixxel-specific.

**Bug 1 — host tools got cross-compiled.** The very first `do_compile`
failed with:
```
/bin/sh: 1: ./cfeconfig_platformdata_tool: Exec format error
```
`cfeconfig_platformdata_tool` is a code generator cFE's *outer* project
builds and then **executes during the build** to produce a `.c` file
(see `cfe/modules/config/tool/CMakeLists.txt`: *"built for the dev host...
executed on the build host during the build"*). Yocto exports `CC`/`CXX`
globally as the aarch64 cross-compiler for every non-native recipe, so the
outer project — which never gets our target toolchain file, only the
inner per-arch sub-build does — picked up the cross-compiler for
*everything*, including this host tool. It got built as an aarch64
binary and couldn't execute on the x86_64 build machine.

Fix: point the outer `cmake -S mission/cfe -B build` invocation at
Yocto's native build-host compiler (`BUILD_CC`/`BUILD_CXX`, standard OE
variables meant for exactly this "tool that runs during the build, not on
the target" scenario) instead of the ambient target `CC`/`CXX`.

**Bug 2 — the native-compiler fix leaked into the target sub-build.**
Fixing bug 1 by `export`-ing `CC=${BUILD_CC}` etc. before the outer
`cmake` call fixed the host tools, but broke the *target* binaries in a
subtler way: `do_package_qa` then failed with
```
doesn't have GNU_HASH (didn't pass LDFLAGS?)
```
and a `buildpaths` warning showing `RUNPATH` pointing at
`.../recipe-sysroot-native/...` inside `pixxel_main.so`. Root cause: the
inner per-arch sub-build is a **subprocess** cFE's cmake spawns via
`execute_process()`, and subprocesses inherit the parent shell's
environment — including the `export`ed native `LDFLAGS`. CMake's own
environment-based cache initialization reads that ambient `$LDFLAGS` and
silently overrides our toolchain file's `SET(CMAKE_..._LINKER_FLAGS ...)`
calls for the inner build, so the *target* apps ended up linked with the
*native* build-host's linker flags (`-L${STAGING_LIBDIR_NATIVE}`, no
`-Wl,--hash-style=gnu`).

Fix: never `export` the native compiler/flags as shell environment
variables. Pass them as `-D` command-line arguments to the outer `cmake`
invocation instead (`-DCMAKE_C_COMPILER=`, `-DCMAKE_C_FLAGS=`, ...) — `-D`
flags only populate that specific `cmake` process's own `CMakeCache.txt`
and are never inherited by child processes, so the inner sub-build's
environment stays target-oriented and its toolchain-file `SET()` calls
win as intended.

**Bug 3 (found alongside bug 2) — `MODULE` vs `SHARED` linker flags.**
Even with bug 2's fix isolated to the outer build, the *first* attempt to
set target linker flags in the toolchain file only set
`CMAKE_SHARED_LINKER_FLAGS`. cFE's `add_cfe_app()` builds each app via
`add_library(${APP_NAME} MODULE ...)` — a `MODULE` library (a `dlopen()`'d
plugin), not `SHARED` — and CMake links `MODULE` targets using the
separate `CMAKE_MODULE_LINKER_FLAGS` variable. Missing that meant the
module builds got no linker flags injected at all. Fixed by setting all
three (`..._EXE_...`, `..._SHARED_...`, `..._MODULE_...`) in the
toolchain file.

### 3.3 Where the sources come from

Unlike the native build, `pixxel-apps_1.0.bb` doesn't run `build.sh` — it
re-implements the same fetch step declaratively via `SRC_URI`:

```bitbake
SRC_URI = " \
    git://github.com/nasa/cFE.git;...;destsuffix=git/mission/cfe \
    git://github.com/nasa/osal.git;...;destsuffix=git/mission/osal \
    git://github.com/nasa/PSP.git;...;destsuffix=git/mission/psp \
    git://github.com/nasa/elf2cfetbl.git;...;destsuffix=git/mission/tools \
    file://pixxel_defs;subdir=git/mission \
    file://startup;subdir=git/mission \
    file://pixxel_controller;subdir=git \
    file://pixxel_main;subdir=git \
    file://pixxel_msgids.h;subdir=git \
"
```

`SRCREV_{cfe,osal,psp,tools} = "${AUTOREV}"` tracks the tip of `main` for
each repo, mirroring `build.sh`'s `--branch main` clone. BitBake lays all
of this out under `${WORKDIR}/git/` in **exactly** the same relative
structure `build.sh` produces under `cfs-apps/` — `mission/{cfe,osal,psp,
tools,pixxel_defs,startup}` alongside `pixxel_controller/`, `pixxel_main/`,
`pixxel_msgids.h`. That's required because `pixxel_defs/targets.cmake` finds
the two apps via a *relative* path (`MISSION_MODULE_SEARCH_PATH ".."`), and
each app's `CMakeLists.txt` includes shared headers via `../`. Preserve the
layout, and both native and cross builds Just Work with the same source
files, unmodified.

---

## 4. Why `files/` are symlinks, not copies

Both recipes' `files/` directories contain **symlinks** into the real
source trees (`driver/`, `cfs-apps/`) rather than duplicated files:

```
meta-pixxel/recipes-kernel/pixxel-driver/files/
  pixxel_platform_driver.c -> .../driver/pixxel_platform_driver.c
  Makefile                 -> .../driver/Makefile

meta-pixxel/recipes-apps/pixxel-apps/files/
  pixxel_controller -> .../cfs-apps/pixxel_controller
  pixxel_main       -> .../cfs-apps/pixxel_main
  pixxel_msgids.h   -> .../cfs-apps/pixxel_msgids.h
  pixxel_defs        -> .../cfs-apps/mission/pixxel_defs
  startup             -> .../cfs-apps/mission/startup
```

This means there is exactly **one copy** of every source file in the repo.
Edit `driver/pixxel_platform_driver.c` or anything under `cfs-apps/`, and
the next `bitbake` invocation picks up the change automatically — no
"remember to copy it into the layer" step, and no risk of the Yocto build
silently testing stale code.

**Caveat:** these are relative symlinks, so they resolve correctly regardless of where the
repository is cloned. If `do_fetch` ever fails with a missing-file error, check these symlinks
first — an absolute-path symlink (or one broken by moving files without `git mv`) is the usual
cause.

---

## 5. BitBake's task model, briefly

Every recipe is a sequence of **tasks** — `do_fetch` → `do_unpack` →
`do_configure` → `do_compile` → `do_install` → `do_package` →
`do_populate_sysroot` / `do_package_write_ipk` — and BitBake builds the
full dependency graph across *all* recipes before running anything. Two
things that surprised us during setup, worth knowing:

- **Task dependencies pull in unrelated-looking work.** Building
  `pixxel-apps` for the first time didn't just fetch cFE — it first built
  `gcc-cross-aarch64`, `binutils-cross-aarch64`, `glibc`, and a chain of
  *native* tools (`openssl-native`, `perl-native`, `cmake-native`, ...)
  because nothing in the aarch64 toolchain existed yet and BitBake builds
  every prerequisite from source when there's no `sstate-cache` hit. This
  is a one-time cost — subsequent builds reuse `sstate-cache/` and skip
  straight to our recipes.
- **Two `bitbake` invocations against the same `build/` directory queue,
  not run in parallel.** There's one persistent `bitbake-server` per build
  directory; a second client command waits for the first to release it.
  This is why `bitbake -e pixxel-driver` appeared to "hang" while
  `pixxel-apps -c configure` was still running — it wasn't stuck, it was
  queued.

To drive a single recipe's tasks directly for debugging (skips whatever
isn't a dependency of the requested task):

```bash
cd poky && source oe-init-build-env build
bitbake pixxel-apps -c fetch       # just fetch SRC_URI
bitbake pixxel-apps -c unpack      # fetch + unpack, inspect the tree
bitbake pixxel-apps -c configure   # + our do_configure (generates toolchain file, runs cmake)
bitbake pixxel-apps -c compile     # + cross-compiles everything
bitbake pixxel-apps -c install     # + our do_install
bitbake pixxel-apps                # full task chain incl. packaging
```

Source for a given task run lands under:
```
poky/build/tmp/work/cortexa57-poky-linux/pixxel-apps/1.0/git/
```
(`cortexa57-poky-linux` is the TUNE-specific arch dir for qemuarm64's
Cortex-A57 tuning — not a typo for `aarch64`.) Logs for a specific task:
```
poky/build/tmp/work/cortexa57-poky-linux/pixxel-apps/1.0/temp/log.do_configure
poky/build/tmp/work/cortexa57-poky-linux/pixxel-apps/1.0/temp/log.do_compile
```

---

## 6. `local.conf` settings for this project

```
MACHINE = "qemuarm64"        # target: the machine the assignment specifies
INHERIT += "rm_work"         # delete each recipe's work dir after a successful
                              # build to bound disk use (sstate-cache is kept,
                              # so rebuilds after `bitbake -c cleanall` are
                              # still fast — only the raw work tree goes away)
```

`bblayers.conf` adds one line — the absolute path to `meta-pixxel` — beside
Poky's three built-in layers (`meta`, `meta-poky`, `meta-yocto-bsp`).

---

## 7. One-time host fixes this build needed (informational)

Not part of the recipe/layer design, but worth knowing if this is set up
on a different machine:

- **AppArmor unprivileged user namespaces** — Ubuntu 24.04 restricts these
  by default; BitBake's `pseudo`/fakeroot sandboxing needs them. Fixed via
  `kernel.apparmor_restrict_unprivileged_userns=0` in
  `/etc/sysctl.d/60-apparmor-userns.conf`. Without this, *any* BitBake
  command fails immediately with `ERROR: User namespaces are not usable by
  BitBake, possibly due to AppArmor.`
- **Host package list** — Yocto's documented Ubuntu host-dependency list
  (`gawk`, `chrpath`, `diffstat`, `texinfo`, `socat`, `libsdl1.2-dev`,
  `python3-git`, `python3-subunit`, `gcc-multilib`, `python3-pip`, plus the
  aarch64 cross-toolchain packages for the native test build) — installing
  fewer than the full list surfaces as
  `ERROR: The following required tools (as specified by HOSTTOOLS) appear
  to be unavailable`, naming exactly what's missing.

---

## 8. What happens after a successful build

```bash
cd poky/build
runqemu qemuarm64 nographic dtb='/home/ash/Downloads/plasg_0107/plasg/qemuarm64 (1).dtb'
```

Inside the booted QEMU shell:
```bash
insmod /lib/modules/*/extra/pixxel_platform_driver.ko
ls /dev/pixxel                 # driver's probe() created this
cd /opt/pixxel/cpu1
./core-cpu1
# Expect: PIXXEL_MAIN: [PASS] device is ENABLED (status=1)
```

This is the point where Layers 1–4 (device tree → driver → cFS apps) all
come together for the first time outside native host testing — everything
before this has been cross-compiling and packaging, not exercising the
actual `pixxel,virt-dev` register simulation.
