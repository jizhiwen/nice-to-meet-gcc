#!/usr/bin/env bash
# Native toolchain shared variables
# Usage: source config.sh

export TOP="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Where to install toolchain binaries (gcc, ld, etc.)
export PREFIX="${PREFIX:-$TOP/native-x86_64}"

# Native build: host == target
export BUILD="${BUILD:-$("$TOP/scripts/config.guess" 2>/dev/null || gcc -dumpmachine)}"
export HOST="$BUILD"
export TARGET="$BUILD"

# Directories
export SRCDIR="$TOP/sources"
export BUILDDIR="$TOP/build"
export LOGDIR="$TOP/logs"

# Versions (keep in sync with build-cross/config.sh)
export BINUTILS_VER=2.43.1
export GCC_VER=14.2.0
export GLIBC_VER=2.40
export LINUX_VER=6.12.5
export GMP_VER=6.3.0
export MPFR_VER=4.2.1
export MPC_VER=1.3.1
export ISL_VER=0.27

# Native: PREFIX is the sysroot
export SYSROOT="$PREFIX"

# Linux kernel ARCH for headers_install (derived from BUILD)
case "$BUILD" in
  x86_64-*) export LINUX_ARCH=x86_64 ;;
  aarch64-*) export LINUX_ARCH=arm64 ;;
  *) export LINUX_ARCH="${LINUX_ARCH:-$(uname -m)}" ;;
esac

export JOBS="${JOBS:-$(nproc)}"

export PATH="$PREFIX/bin:$PATH"
