#!/usr/bin/env bash
# aarch64-none-linux-gnu 交叉工具链构建配置
# 用法: source config.sh

export TOP="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 安装前缀（工具链可执行文件安装位置）
export PREFIX="${PREFIX:-$HOME/cross-aarch64}"

# GNU 目标三元组（arm64 在 GNU 中写作 aarch64）
export TARGET="${TARGET:-aarch64-none-linux-gnu}"

# 构建机三元组（自动检测）
export BUILD="${BUILD:-$("$TOP/scripts/config.guess" 2>/dev/null || gcc -dumpmachine)}"
export HOST="$BUILD"

# 源码与构建目录
export SRCDIR="$TOP/sources"
export BUILDDIR="$TOP/build"
export LOGDIR="$TOP/logs"

# 组件版本（可按需修改）
export BINUTILS_VER=2.43.1
export GCC_VER=14.2.0
export GLIBC_VER=2.40
export LINUX_VER=6.12.5
export GMP_VER=6.3.0
export MPFR_VER=4.2.1
export MPC_VER=1.3.1
export ISL_VER=0.27

# sysroot：目标系统的根文件系统布局
export SYSROOT="$PREFIX/$TARGET/sysroot"

# 并行编译线程数
export JOBS="${JOBS:-$(nproc)}"

# 将工具链加入 PATH（构建过程中需要）
export PATH="$PREFIX/bin:$PATH"
