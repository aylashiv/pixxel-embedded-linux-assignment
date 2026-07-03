SUMMARY = "Pixxel cFS mission for qemuarm64"
DESCRIPTION = "Cross-compiles the full NASA cFS mission (cFE core + OSAL + PSP) \
               together with the pixxel_main and pixxel_controller apps that \
               exercise the pixxel_platform_driver kernel module."
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

DEPENDS = "cmake-native"

# Same repos/branch build.sh clones for the native test build (see
# cfs-apps/mission/build.sh). AUTOREV tracks the tip of "main", matching
# build.sh's `git clone --depth 1 --branch main`.
SRCREV_cfe = "${AUTOREV}"
SRCREV_osal = "${AUTOREV}"
SRCREV_psp = "${AUTOREV}"
SRCREV_tools = "${AUTOREV}"
SRCREV_FORMAT = "cfe_osal_psp_tools"

# Layout mirrors cfs-apps/ exactly, so pixxel_defs' relative
# MISSION_MODULE_SEARCH_PATH ".." and the apps' CMakeLists.txt "../" include
# paths resolve the same way they do for the native build:
#
#   ${S}/mission/{cfe,osal,psp,tools,pixxel_defs,startup}
#   ${S}/pixxel_controller, ${S}/pixxel_main, ${S}/pixxel_msgids.h
SRC_URI = " \
    git://github.com/nasa/cFE.git;protocol=https;branch=main;name=cfe;destsuffix=git/mission/cfe \
    git://github.com/nasa/osal.git;protocol=https;branch=main;name=osal;destsuffix=git/mission/osal \
    git://github.com/nasa/PSP.git;protocol=https;branch=main;name=psp;destsuffix=git/mission/psp \
    git://github.com/nasa/elf2cfetbl.git;protocol=https;branch=main;name=tools;destsuffix=git/mission/tools \
    file://pixxel_defs;subdir=git/mission \
    file://startup;subdir=git/mission \
    file://pixxel_controller;subdir=git \
    file://pixxel_main;subdir=git \
    file://pixxel_msgids.h;subdir=git \
"

S = "${WORKDIR}/git"

# cFE's cmake is not a plain single-project build (it configures a
# sub-build per target arch via execute_process()), so we drive it directly
# instead of inheriting the cmake bbclass. PIXXEL_CROSS_BUILD tells
# pixxel_defs/targets.cmake to select cpu1_SYSTEM=aarch64-linux-gnu, which
# in turn makes cFE's cmake look for toolchain-aarch64-linux-gnu.cmake in
# pixxel_defs/ — generated below from the Yocto cross-compiler variables.
do_configure() {
    mission_dir="${S}/mission"
    defs_dir="$mission_dir/pixxel_defs"

    # Start from a clean build tree every time: CMake caches the compiler
    # choice in CMakeCache.txt, so re-running configure against a stale
    # build/ dir would silently keep whatever compiler the previous run
    # used instead of picking up CC/BUILD_CC set below.
    rm -rf "${S}/build"

    cc_path=$(echo ${CC} | awk '{print $1}')
    cc_extra=$(echo ${CC} | cut -s -f 2- -d' ')
    cxx_path=$(echo ${CXX} | awk '{print $1}')
    cxx_extra=$(echo ${CXX} | cut -s -f 2- -d' ')

    cat > "$defs_dir/toolchain-aarch64-linux-gnu.cmake" <<EOF
SET(CMAKE_SYSTEM_NAME      Linux)
SET(CMAKE_SYSTEM_VERSION   1)
SET(CMAKE_SYSTEM_PROCESSOR aarch64)

SET(CMAKE_C_COMPILER   "$cc_path")
SET(CMAKE_CXX_COMPILER "$cxx_path")
SET(CMAKE_C_FLAGS      "$cc_extra ${CFLAGS}")
SET(CMAKE_CXX_FLAGS     "$cxx_extra ${CXXFLAGS}")
SET(CMAKE_EXE_LINKER_FLAGS    "${LDFLAGS}")
SET(CMAKE_SHARED_LINKER_FLAGS "${LDFLAGS}")
# cFE's add_cfe_app() builds each app as a CMake MODULE library (dlopen()'d
# plugin, not a linkable shared object), which CMake links using
# CMAKE_MODULE_LINKER_FLAGS — a variable distinct from
# CMAKE_SHARED_LINKER_FLAGS. Without this, module builds silently got no
# LDFLAGS at all (missing -Wl,--hash-style=gnu etc.), which Yocto's
# packaging QA (do_package_qa) correctly flags as a fatal error.
SET(CMAKE_MODULE_LINKER_FLAGS "${LDFLAGS}")

SET(CMAKE_FIND_ROOT_PATH "${STAGING_DIR_TARGET}")
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# cFE/OSAL abstraction layers to build against this toolchain — the target
# is a full Linux userspace under QEMU, so the existing pc-linux PSP/posix
# OSAL (same ones the native build uses) apply unchanged.
SET(CFE_SYSTEM_PSPNAME "pc-linux")
SET(OSAL_SYSTEM_OSTYPE "posix")
EOF

    # cFE's *outer* (mission-scope) CMake project builds a few host-side
    # code-generator tools that must run on the build machine during the
    # build itself (e.g. cfeconfig_platformdata_tool — see its
    # CMakeLists.txt comment: "built for the dev host, not cross
    # compiled"). Only the *inner* per-arch sub-build (launched internally
    # by cFE via execute_process() with -DCMAKE_TOOLCHAIN_FILE=<the file
    # generated above>) should target aarch64. So the outer invocation
    # here must use Yocto's native build-host compiler, not the ambient
    # target CC/CXX — otherwise those host tools get cross-compiled and
    # fail with "Exec format error" when the build tries to run them.
    #
    # This is passed via -D flags on the outer cmake command line, NOT via
    # exported CC/CXX/CFLAGS/LDFLAGS shell variables: cFE's inner sub-build
    # is a *nested* `cmake` subprocess (execute_process()) that inherits
    # this same shell's environment. An exported ambient $LDFLAGS gets read
    # by CMake's own environment-based cache initialization and silently
    # overrides the inner sub-build's toolchain-file SET() calls above —
    # which is exactly what caused the inner (target) build to link with
    # the native BUILD_LDFLAGS instead (missing -Wl,--hash-style=gnu,
    # RUNPATH pointing at recipe-sysroot-native). Passing -D flags instead
    # scopes the override to the outer project's own CMakeCache only.
    build_cc_path=$(echo ${BUILD_CC} | awk '{print $1}')
    build_cc_extra=$(echo ${BUILD_CC} | cut -s -f 2- -d' ')
    build_cxx_path=$(echo ${BUILD_CXX} | awk '{print $1}')
    build_cxx_extra=$(echo ${BUILD_CXX} | cut -s -f 2- -d' ')

    export PIXXEL_CROSS_BUILD=1
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${S}/build/install" \
        -DCMAKE_C_COMPILER="$build_cc_path" \
        -DCMAKE_CXX_COMPILER="$build_cxx_path" \
        -DCMAKE_C_FLAGS="$build_cc_extra ${BUILD_CFLAGS}" \
        -DCMAKE_CXX_FLAGS="$build_cxx_extra ${BUILD_CXXFLAGS}" \
        -DCMAKE_EXE_LINKER_FLAGS="${BUILD_LDFLAGS}" \
        -S "$mission_dir/cfe" \
        -B "${S}/build"
}

do_compile() {
    # Compiler selection for both the outer (native host-tool) and inner
    # (aarch64 target) sub-builds is already cached in CMakeCache.txt from
    # do_configure — no need to re-export CC/CXX here.
    export PIXXEL_CROSS_BUILD=1
    cmake --build "${S}/build" --target mission-install -- ${PARALLEL_MAKE}
}

do_install() {
    install -d ${D}/opt/pixxel/cpu1
    cp -r ${S}/build/install/cpu1/. ${D}/opt/pixxel/cpu1/
    install -d ${D}/opt/pixxel/cpu1/cf
    install -m 0644 ${S}/mission/startup/cfe_es_startup.scr ${D}/opt/pixxel/cpu1/cf/
}

FILES:${PN} += "/opt/pixxel"

# The cFE/app .so files are runtime-dlopen()'d plugins loaded by the
# startup script, not conventional linkable shared libraries — there is
# no matching -dev package and no SONAME versioning, which is expected.
INSANE_SKIP:${PN} += "dev-so"
