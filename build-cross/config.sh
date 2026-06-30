#!/usr/bin/env bash
# Cross-toolchain shared variables
# Usage: source config.sh

export TOP="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Where to install toolchain binaries (gcc, ld, etc.)
# Default: inside this build-cross directory
export PREFIX="${PREFIX:-$TOP/cross-aarch64}"

# Target: arm64 Linux (GNU spelling is aarch64)
export TARGET="${TARGET:-aarch64-none-linux-gnu}"

# Build machine triplet (usually x86_64-linux-gnu)
export BUILD="${BUILD:-$("$TOP/scripts/config.guess" 2>/dev/null || gcc -dumpmachine)}"
export HOST="$BUILD"

# Directories
export SRCDIR="$TOP/sources"    # extracted source trees
export BUILDDIR="$TOP/build"    # out-of-tree build dirs
export LOGDIR="$TOP/logs"       # build logs

# Versions
export BINUTILS_VER=2.43.1
export GCC_VER=14.2.0
export GLIBC_VER=2.40
export LINUX_VER=6.12.5
export GMP_VER=6.3.0
export MPFR_VER=4.2.1
export MPC_VER=1.3.1
export ISL_VER=0.27

# sysroot: pretend this is the root of an aarch64 system; headers and libc go here
export SYSROOT="$PREFIX/$TARGET/sysroot"

export JOBS="${JOBS:-$(nproc)}"

# Put cross tools on PATH as each step installs them
export PATH="$PREFIX/bin:$PATH"
