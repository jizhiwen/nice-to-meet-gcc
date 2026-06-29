#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/config.sh"

mkdir -p "$SRCDIR" "$BUILDDIR" "$LOGDIR"

download() {
  local url="$1"
  local out="$2"
  if [[ -f "$out" ]]; then
    echo "  已存在: $(basename "$out")"
    return 0
  fi
  echo "  下载: $url"
  curl -fL --retry 3 --retry-delay 5 -o "$out" "$url"
}

extract() {
  local archive="$1"
  local dir="$2"
  if [[ -d "$dir" ]]; then
    echo "  已解压: $(basename "$dir")"
    return 0
  fi
  echo "  解压: $(basename "$archive")"
  tar -xf "$archive" -C "$SRCDIR"
}

echo "=== 下载源码 ==="

download "https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VER}.tar.xz" \
         "$SRCDIR/binutils-${BINUTILS_VER}.tar.xz"
download "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.tar.xz" \
         "$SRCDIR/gcc-${GCC_VER}.tar.xz"
download "https://ftp.gnu.org/gnu/glibc/glibc-${GLIBC_VER}.tar.xz" \
         "$SRCDIR/glibc-${GLIBC_VER}.tar.xz"
download "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${LINUX_VER}.tar.xz" \
         "$SRCDIR/linux-${LINUX_VER}.tar.xz"
download "https://ftp.gnu.org/gnu/gmp/gmp-${GMP_VER}.tar.xz" \
         "$SRCDIR/gmp-${GMP_VER}.tar.xz"
download "https://ftp.gnu.org/gnu/mpfr/mpfr-${MPFR_VER}.tar.xz" \
         "$SRCDIR/mpfr-${MPFR_VER}.tar.xz"
download "https://ftp.gnu.org/gnu/mpc/mpc-${MPC_VER}.tar.gz" \
         "$SRCDIR/mpc-${MPC_VER}.tar.gz"
download "https://libisl.sourceforge.io/isl-${ISL_VER}.tar.xz" \
         "$SRCDIR/isl-${ISL_VER}.tar.xz"

echo "=== 解压源码 ==="
extract "$SRCDIR/binutils-${BINUTILS_VER}.tar.xz" "$SRCDIR/binutils-${BINUTILS_VER}"
extract "$SRCDIR/gcc-${GCC_VER}.tar.xz"         "$SRCDIR/gcc-${GCC_VER}"
extract "$SRCDIR/glibc-${GLIBC_VER}.tar.xz"     "$SRCDIR/glibc-${GLIBC_VER}"
extract "$SRCDIR/linux-${LINUX_VER}.tar.xz"     "$SRCDIR/linux-${LINUX_VER}"
extract "$SRCDIR/gmp-${GMP_VER}.tar.xz"         "$SRCDIR/gmp-${GMP_VER}"
extract "$SRCDIR/mpfr-${MPFR_VER}.tar.xz"       "$SRCDIR/mpfr-${MPFR_VER}"
extract "$SRCDIR/mpc-${MPC_VER}.tar.gz"         "$SRCDIR/mpc-${MPC_VER}"
extract "$SRCDIR/isl-${ISL_VER}.tar.xz"         "$SRCDIR/isl-${ISL_VER}"

echo "=== 链接 GCC 依赖库 ==="
GCC_DIR="$SRCDIR/gcc-${GCC_VER}"
for lib in gmp mpfr mpc isl; do
  ver_var="${lib^^}_VER"
  ln -sf "../${lib}-${!ver_var}" "$GCC_DIR/$lib"
done

echo "=== 下载完成 ==="
echo "源码目录: $SRCDIR"
