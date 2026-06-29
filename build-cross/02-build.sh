#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/config.sh"

log() {
  echo "[$(date '+%H:%M:%S')] $*"
}

run_step() {
  local name="$1"
  shift
  log ">>> $name"
  "$@" 2>&1 | tee "$LOGDIR/$(echo "$name" | tr ' /' '__').log"
  log "<<< $name 完成"
}

# ---------------------------------------------------------------------------
# 步骤 0: 检查构建依赖
# ---------------------------------------------------------------------------
check_deps() {
  log "检查构建依赖..."
  local missing=()
  for cmd in gcc g++ make patch bison flex gawk python3 curl tar xz; do
    command -v "$cmd" >/dev/null 2>&1 || missing+=("$cmd")
  done
  if ((${#missing[@]})); then
    echo "缺少依赖: ${missing[*]}"
    echo "Ubuntu/Debian 安装命令:"
    echo "  sudo apt install build-essential bison flex gawk texinfo \\"
    echo "    libgmp-dev libmpfr-dev libmpc-dev zlib1g-dev \\"
    echo "    curl xz-utils patch python3"
    exit 1
  fi
}

# ---------------------------------------------------------------------------
# 步骤 1: Linux 内核头文件 → sysroot
# ---------------------------------------------------------------------------
step_linux_headers() {
  local dir="$BUILDDIR/linux-headers"
  mkdir -p "$dir"
  cd "$dir"
  make -C "$SRCDIR/linux-${LINUX_VER}" \
    ARCH=arm64 \
    INSTALL_HDR_PATH="$SYSROOT/usr" \
    headers_install
}

# ---------------------------------------------------------------------------
# 步骤 2: Binutils
# ---------------------------------------------------------------------------
step_binutils() {
  local dir="$BUILDDIR/binutils"
  rm -rf "$dir"
  mkdir -p "$dir"
  cd "$dir"
  "$SRCDIR/binutils-${BINUTILS_VER}/configure" \
    --prefix="$PREFIX" \
    --target="$TARGET" \
    --with-sysroot="$SYSROOT" \
    --disable-multilib \
    --disable-nls \
    --disable-werror
  make -j"$JOBS"
  make install
}

# ---------------------------------------------------------------------------
# 步骤 3: GCC 第一阶段 — 最小 C 交叉编译器（无头文件、无共享库）
# ---------------------------------------------------------------------------
step_gcc1() {
  local dir="$BUILDDIR/gcc-stage1"
  rm -rf "$dir"
  mkdir -p "$dir"
  cd "$dir"
  "$SRCDIR/gcc-${GCC_VER}/configure" \
    --prefix="$PREFIX" \
    --target="$TARGET" \
    --build="$BUILD" \
    --host="$HOST" \
    --with-sysroot="$SYSROOT" \
    --with-newlib \
    --without-headers \
    --enable-languages=c \
    --disable-shared \
    --disable-threads \
    --disable-nls \
    --disable-bootstrap \
    --disable-libatomic \
    --disable-libgomp \
    --disable-libitm \
    --disable-libquadmath \
    --disable-libsanitizer \
    --disable-libssp \
    --disable-libvtv \
    --disable-multilib \
    --with-system-zlib
  make -j"$JOBS" all-gcc all-target-libgcc
  make install-gcc install-target-libgcc
}

# ---------------------------------------------------------------------------
# 步骤 4: Glibc 头文件 + 启动文件 + 占位 libc.so
# ---------------------------------------------------------------------------
step_glibc_headers() {
  local dir="$BUILDDIR/glibc-headers"
  rm -rf "$dir"
  mkdir -p "$dir"
  cd "$dir"
  "$SRCDIR/glibc-${GLIBC_VER}/configure" \
    --prefix=/usr \
    --host="$TARGET" \
    --build="$BUILD" \
    --with-headers="$SYSROOT/usr/include" \
    --disable-multilib \
    --disable-nls \
    --disable-werror \
    --enable-kernel=4.19 \
    libc_cv_forced_unwind=yes \
    libc_cv_c_cleanup=yes
  make -j"$JOBS" install-bootstrap-headers=yes install-headers DESTDIR="$SYSROOT"
  make -j"$JOBS" csu/subdir_lib
  install -D csu/crt1.o  "$SYSROOT/usr/lib/crt1.o"
  install -D csu/crti.o  "$SYSROOT/usr/lib/crti.o"
  install -D csu/crtn.o  "$SYSROOT/usr/lib/crtn.o"
  # 占位文件，供 GCC 第二阶段链接使用
  "${TARGET}-gcc" -nostdlib -nostartfiles -shared -x c /dev/null \
    -o "$SYSROOT/usr/lib/libc.so"
  touch "$SYSROOT/usr/include/gnu/stubs.h"
}

# ---------------------------------------------------------------------------
# 步骤 5: 完整 libgcc（含线程支持等）
# ---------------------------------------------------------------------------
step_libgcc() {
  local dir="$BUILDDIR/gcc-stage1"
  cd "$dir"
  make -j"$JOBS" all-target-libgcc
  make install-target-libgcc
}

# ---------------------------------------------------------------------------
# 步骤 6: 完整 Glibc
# ---------------------------------------------------------------------------
step_glibc() {
  local dir="$BUILDDIR/glibc"
  rm -rf "$dir"
  mkdir -p "$dir"
  cd "$dir"
  "$SRCDIR/glibc-${GLIBC_VER}/configure" \
    --prefix=/usr \
    --host="$TARGET" \
    --build="$BUILD" \
    --with-headers="$SYSROOT/usr/include" \
    --disable-multilib \
    --disable-nls \
    --disable-werror \
    --enable-kernel=4.19 \
    --with-default-link \
    libc_cv_forced_unwind=yes \
    libc_cv_c_cleanup=yes
  make -j"$JOBS"
  make DESTDIR="$SYSROOT" install
}

# ---------------------------------------------------------------------------
# 步骤 7: GCC 第二阶段 — 完整工具链（C/C++、共享库）
# ---------------------------------------------------------------------------
step_gcc2() {
  local dir="$BUILDDIR/gcc-stage2"
  rm -rf "$dir"
  mkdir -p "$dir"
  cd "$dir"
  "$SRCDIR/gcc-${GCC_VER}/configure" \
    --prefix="$PREFIX" \
    --target="$TARGET" \
    --build="$BUILD" \
    --host="$HOST" \
    --with-sysroot="$SYSROOT" \
    --enable-languages=c,c++ \
    --enable-shared \
    --enable-threads \
    --enable-tls \
    --disable-nls \
    --disable-bootstrap \
    --disable-multilib \
    --with-system-zlib
  make -j"$JOBS"
  make install
}

# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------
usage() {
  cat <<EOF
用法: $0 [步骤]

步骤:
  all            执行全部步骤（默认）
  deps           检查依赖
  headers        Linux 内核头文件
  binutils       交叉 Binutils
  gcc1           GCC 第一阶段
  glibc-h        Glibc 头文件与启动文件
  libgcc         完整 libgcc
  glibc          完整 Glibc
  gcc2           GCC 第二阶段（最终工具链）

环境变量:
  PREFIX         安装前缀（默认 \$HOME/cross-aarch64）
  JOBS           并行线程数（默认 nproc）
  TARGET         目标三元组（默认 aarch64-none-linux-gnu）

示例:
  source config.sh && ./02-build.sh all
  ./02-build.sh gcc2          # 从某步恢复
EOF
}

main() {
  local step="${1:-all}"
  mkdir -p "$BUILDDIR" "$LOGDIR" "$SYSROOT/usr/include" "$SYSROOT/usr/lib"

  case "$step" in
    deps)       check_deps ;;
    headers)    run_step "linux-headers" step_linux_headers ;;
    binutils)   run_step "binutils" step_binutils ;;
    gcc1)       run_step "gcc-stage1" step_gcc1 ;;
    glibc-h)    run_step "glibc-headers" step_glibc_headers ;;
    libgcc)     run_step "libgcc" step_libgcc ;;
    glibc)      run_step "glibc" step_glibc ;;
    gcc2)       run_step "gcc-stage2" step_gcc2 ;;
    all)
      check_deps
      run_step "linux-headers" step_linux_headers
      run_step "binutils" step_binutils
      run_step "gcc-stage1" step_gcc1
      run_step "glibc-headers" step_glibc_headers
      run_step "libgcc" step_libgcc
      run_step "glibc" step_glibc
      run_step "gcc-stage2" step_gcc2
      log "工具链安装完成: $PREFIX"
      log "请将以下行加入 ~/.bashrc:"
      echo "  export PATH=\"$PREFIX/bin:\$PATH\""
      ;;
    -h|--help|help) usage ;;
    *)
      echo "未知步骤: $step"
      usage
      exit 1
      ;;
  esac
}

main "$@"
