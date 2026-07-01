#!/usr/bin/env bash
# boot_kernel — minimal Linux kernel + bash initramfs
# Usage: source config.sh

export TOP="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export ARCH="${ARCH:-x86_64}"
export LINUX_VER="${LINUX_VER:-6.12.5}"
export BASH_VER="${BASH_VER:-5.2.21}"
export BUSYBOX_VER="${BUSYBOX_VER:-1.36.1}"
# mini = key applets only (configs/busybox-minimal.applets); full = defconfig
export BUSYBOX_PROFILE="${BUSYBOX_PROFILE:-mini}"

export SRCDIR="$TOP/sources"
export BUILDDIR="$TOP/build"
export LOGDIR="$TOP/logs"
export OUTPUT="$TOP/output"
export ROOTFS="$BUILDDIR/rootfs"

export KERNEL_SRC="$SRCDIR/linux-${LINUX_VER}"
export KERNEL_BUILD="$BUILDDIR/kernel"
export BASH_SRC="$SRCDIR/bash-${BASH_VER}"
export BASH_BUILD="$BUILDDIR/bash"
export BASH_INSTALL="$BUILDDIR/bash-install"
export BASH_BIN="$BASH_INSTALL/bin/bash"
export BUSYBOX_SRC="$SRCDIR/busybox-${BUSYBOX_VER}"
export BUSYBOX_BUILD="$BUILDDIR/busybox"
export INITRAMFS="$OUTPUT/initramfs.cpio.gz"
export BZIMAGE="$OUTPUT/bzImage"

export JOBS="${JOBS:-$(nproc)}"

# QEMU
export QEMU="${QEMU:-qemu-system-x86_64}"
export QEMU_MEM="${QEMU_MEM:-512M}"
export QEMU_APPEND="${QEMU_APPEND:-console=ttyS0 rdinit=/init init=/init panic=1}"
