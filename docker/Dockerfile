# Yocto build environment for the pixxel mission
# Base: Ubuntu 22.04 — most tested with Yocto Scarthgap (5.0 LTS)
#
# Does NOT contain poky or the build tree — those are mounted at runtime
# from the host (NVMe source, pendrive build output) to keep the image small.

FROM ubuntu:22.04

# Prevent apt from prompting during package install
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

# ── All packages required by Yocto Scarthgap on Ubuntu 22.04 ──────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Core build tools
    build-essential \
    gcc g++ make \
    git wget curl \
    # Yocto-specific tools
    diffstat \
    chrpath \
    cpio \
    file \
    socat \
    texinfo \
    xz-utils \
    zstd \
    lz4 \
    # Python
    python3 \
    python3-pip \
    python3-pexpect \
    python3-git \
    python3-jinja2 \
    python3-subunit \
    # Compression / archive
    gzip \
    bzip2 \
    tar \
    unzip \
    # QEMU for runqemu
    qemu-system-arm \
    qemu-system-aarch64 \
    qemu-utils \
    # Locale (Yocto requires en_US.UTF-8)
    locales \
    # Misc
    iputils-ping \
    iproute2 \
    libssl-dev \
    libsdl1.2-dev \
    rpcsvc-proto \
 && locale-gen en_US.UTF-8 \
 && apt-get clean \
 && rm -rf /var/lib/apt/lists/*

ENV LANG=en_US.UTF-8
ENV LANGUAGE=en_US:en
ENV LC_ALL=en_US.UTF-8

# ── Create a non-root build user ──────────────────────────────────────────
# Yocto refuses to run as root. The UID/GID are set to match the host user
# so that mounted volume files have correct ownership.
ARG HOST_UID=1000
ARG HOST_GID=1000

RUN groupadd -g ${HOST_GID} yoctobuilder \
 && useradd -u ${HOST_UID} -g ${HOST_GID} -m -s /bin/bash yoctobuilder

USER yoctobuilder
WORKDIR /workdir

# Default command — drop into bash so the user can run bitbake interactively
CMD ["/bin/bash"]
