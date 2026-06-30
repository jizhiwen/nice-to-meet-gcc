#!/usr/bin/env bash
# =============================================================================
# aarch64-none-linux-gnu cross-toolchain — step-by-step build script
#
# Why so many steps?
#   GCC and Glibc depend on each other: Glibc needs GCC, full GCC needs Glibc.
#   Cross builds cannot bootstrap like native builds; we break the cycle by hand.
#
# Order (matches README Section 4):
#   1. Linux headers   →  linux/*.h in sysroot
#   2. Binutils          →  aarch64 as, ld
#   3. GCC stage 1       →  cross C only (no libc yet)
#   4. Glibc headers     →  crt*.o, headers, stub libc.so
#   5. libgcc            →  threads / runtime support
#   6. Full Glibc        →  real libc.so
#   7. GCC stage 2       →  full gcc/g++ with shared libs
# =============================================================================
set -Eeuo pipefail

source "$(dirname "$0")/config.sh"

log() { echo "[$(date '+%H:%M:%S')] $*"; }

export STAMPDIR="$BUILDDIR/.stamps"
export LOCKFILE="$BUILDDIR/.build.lock"
LAST_LOG=""
ON_ERROR_SEEN=0

on_error() {
  local exit_code=$?
  if [[ "$ON_ERROR_SEEN" == "1" ]]; then
    exit "$exit_code"
  fi
  ON_ERROR_SEEN=1
  trap - ERR
  echo
  log "Build failed (exit code: $exit_code)."
  if [[ -n "$LAST_LOG" ]]; then
    log "Check log for details: $LAST_LOG"
  fi
  log "Tip: fix the issue, then re-run the failed step only (example: ./02-build.sh glibc)."
  exit "$exit_code"
}
trap on_error ERR

acquire_lock() {
  exec 9>"$LOCKFILE"
  if ! flock -n 9; then
    echo "Another build-cross job is already running."
    echo "Wait for it to finish, or stop it before retrying."
    exit 1
  fi
}

invalidate_later_stamps() {
  local current="$1"
  case "$current" in
    1) rm -f "$STAMPDIR"/2-* "$STAMPDIR"/3-* "$STAMPDIR"/4-* "$STAMPDIR"/5-* "$STAMPDIR"/6-* "$STAMPDIR"/7-* ;;
    2) rm -f "$STAMPDIR"/3-* "$STAMPDIR"/4-* "$STAMPDIR"/5-* "$STAMPDIR"/6-* "$STAMPDIR"/7-* ;;
    3) rm -f "$STAMPDIR"/4-* "$STAMPDIR"/5-* "$STAMPDIR"/6-* "$STAMPDIR"/7-* ;;
    4) rm -f "$STAMPDIR"/5-* "$STAMPDIR"/6-* "$STAMPDIR"/7-* ;;
    5) rm -f "$STAMPDIR"/6-* "$STAMPDIR"/7-* ;;
    6) rm -f "$STAMPDIR"/7-* ;;
    7) ;;
  esac
}

run_step() {
  local display_name="$1"
  local stamp_name="$2"
  local step_no="$3"
  shift 3

  local logfile="$LOGDIR/${display_name}.log"
  local stampfile="$STAMPDIR/${stamp_name}"
  LAST_LOG="$logfile"

  if [[ -f "$stampfile" && "${FORCE:-0}" != "1" ]]; then
    log ">>> $display_name (skipped; already completed)"
    return 0
  fi

  log ">>> $display_name"
  "$@" 2>&1 | tee "$logfile"
  touch "$stampfile"
  invalidate_later_stamps "$step_no"
  log "<<< $display_name done"
}

# ---------------------------------------------------------------------------
# Step 0: check host build dependencies
# ---------------------------------------------------------------------------
check_deps() {
  log "Checking build dependencies..."
  local missing=()
  for cmd in gcc g++ make patch bison flex gawk python3 curl tar xz file flock; do
    command -v "$cmd" >/dev/null 2>&1 || missing+=("$cmd")
  done
  if ((${#missing[@]})); then
    echo "Missing: ${missing[*]}"
    echo "Ubuntu/Debian: sudo apt install build-essential bison flex gawk texinfo \\"
    echo "  libgmp-dev libmpfr-dev libmpc-dev zlib1g-dev curl xz-utils patch python3"
    exit 1
  fi
}

check_sources() {
  local missing=()
  local needed_dirs=(
    "$SRCDIR/linux-${LINUX_VER}"
    "$SRCDIR/binutils-${BINUTILS_VER}"
    "$SRCDIR/gcc-${GCC_VER}"
    "$SRCDIR/glibc-${GLIBC_VER}"
  )

  for dir in "${needed_dirs[@]}"; do
    [[ -d "$dir" ]] || missing+=("$dir")
  done

  if ((${#missing[@]})); then
    echo "Missing extracted source directories:"
    printf '  - %s\n' "${missing[@]}"
    echo "Run: ./01-download.sh"
    exit 1
  fi
}

# ---------------------------------------------------------------------------
# Step 1: Linux kernel headers
# Glibc needs kernel UAPI headers such as <linux/*.h>
# ---------------------------------------------------------------------------
step_linux_headers() {
  mkdir -p "$SYSROOT/usr"
  make -C "$SRCDIR/linux-${LINUX_VER}" \
    ARCH=arm64 \
    INSTALL_HDR_PATH="$SYSROOT/usr" \
    headers_install
}

# ---------------------------------------------------------------------------
# Step 2: Binutils (assembler, linker, ...)
# Produces ${TARGET}-as, ${TARGET}-ld for linking aarch64 objects
# ---------------------------------------------------------------------------
step_binutils() {
  local dir="$BUILDDIR/binutils"
  rm -rf "$dir" && mkdir -p "$dir" && cd "$dir"

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
# Step 3: GCC stage 1 — minimal cross C compiler
# Emits aarch64 code but has no standard library headers yet.
# --without-headers: no libc headers
# --enable-languages=c: C only; C++ comes after Glibc is installed
# Keep this step minimal: build/install compiler only.
# Target libgcc is built in Step 5 after Glibc headers are in place.
# ---------------------------------------------------------------------------
step_gcc1() {
  local dir="$BUILDDIR/gcc-stage1"
  rm -rf "$dir" && mkdir -p "$dir" && cd "$dir"

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
    --disable-multilib \
    --with-system-zlib

  make -j"$JOBS" all-gcc
  make install-gcc
}

# ---------------------------------------------------------------------------
# Step 4: Glibc headers + startup files + stub libc.so
# Lets GCC stage 2 see headers and crt1.o for link tests.
#
# CXX (important):
#   ${TARGET}-g++ does not exist yet. CXX= alone is NOT enough: configure still
#   finds host g++ and the link test passes. Use libc_cv_cxx_link_ok=no so CXX
#   stays empty and Glibc builds links-dso-program-c (pure C) instead.
# ---------------------------------------------------------------------------
step_glibc_headers() {
  local dir="$BUILDDIR/glibc-headers"
  rm -rf "$dir" && mkdir -p "$dir" && cd "$dir"

  CC="${TARGET}-gcc" \
  libc_cv_cxx_link_ok=no \
  "$SRCDIR/glibc-${GLIBC_VER}/configure" \
    --prefix=/usr \
    --host="$TARGET" \
    --build="$BUILD" \
    --with-headers="$SYSROOT/usr/include" \
    --disable-werror \
    --enable-kernel=4.19
    # libc_cv_forced_unwind=yes \
    # libc_cv_c_cleanup=yes

  make -j"$JOBS" install-bootstrap-headers=yes install-headers DESTDIR="$SYSROOT"
  make -j"$JOBS" csu/subdir_lib
  install -D csu/crt1.o "$SYSROOT/usr/lib/crt1.o"
  install -D csu/crti.o "$SYSROOT/usr/lib/crti.o"
  install -D csu/crtn.o "$SYSROOT/usr/lib/crtn.o"

  # Stub libc.so so GCC stage 2 configure can pass link tests
  "${TARGET}-gcc" -nostdlib -nostartfiles -shared -x c /dev/null \
    -o "$SYSROOT/usr/lib/libc.so"
  touch "$SYSROOT/usr/include/gnu/stubs.h"
}

# ---------------------------------------------------------------------------
# Step 5: build libgcc
# Build target libgcc after Glibc headers exist (TLS, threads, etc.)
# ---------------------------------------------------------------------------
step_libgcc() {
  cd "$BUILDDIR/gcc-stage1"
  make -j"$JOBS" all-target-libgcc
  make install-target-libgcc
}

# ---------------------------------------------------------------------------
# Step 6: full Glibc
# Build and install real libc.so, libm.so, ... into sysroot.
# Same libc_cv_cxx_link_ok=no as step 4 (CXX= alone is not enough).
# ---------------------------------------------------------------------------
step_glibc() {
  local dir="$BUILDDIR/glibc"
  rm -rf "$dir" && mkdir -p "$dir" && cd "$dir"

  CC="${TARGET}-gcc" \
  libc_cv_cxx_link_ok=no \
  "$SRCDIR/glibc-${GLIBC_VER}/configure" \
    --prefix=/usr \
    --host="$TARGET" \
    --build="$BUILD" \
    --with-headers="$SYSROOT/usr/include" \
    --disable-werror \
    --enable-kernel=4.19 \
    --with-default-link
    # libc_cv_forced_unwind=yes \
    # libc_cv_c_cleanup=yes

  make -j"$JOBS"
  make DESTDIR="$SYSROOT" install
}

# ---------------------------------------------------------------------------
# Step 7: GCC stage 2 — full toolchain
# Glibc is ready; build gcc/g++ with C++, shared libs, and threads
# ---------------------------------------------------------------------------
step_gcc2() {
  local dir="$BUILDDIR/gcc-stage2"
  local gcc_jobs="${JOBS_GCC2:-$JOBS}"
  rm -rf "$dir" && mkdir -p "$dir" && cd "$dir"

  "$SRCDIR/gcc-${GCC_VER}/configure" \
    --prefix="$PREFIX" \
    --target="$TARGET" \
    --build="$BUILD" \
    --host="$HOST" \
    --with-sysroot="$SYSROOT" \
    --enable-languages=c,c++ \
    --disable-nls \
    --disable-bootstrap \
    --disable-multilib \
    --with-system-zlib

  make -j"$gcc_jobs"
  make install
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
usage() {
  cat <<EOF
Usage: $0 [step]

Steps (in order):
  all       Run all steps (default)
  deps      Check dependencies
  headers   1. Linux headers
  binutils  2. Binutils
  gcc1      3. GCC stage 1
  glibc-h   4. Glibc headers and startup files
  libgcc    5. Finish libgcc
  glibc     6. Full Glibc
  gcc2      7. GCC stage 2
  list      Show step completion status
  clean     Remove build dirs and completion stamps

Environment: PREFIX  TARGET  JOBS  FORCE=1  (see config.sh)
             JOBS_GCC2 can override JOBS for the last GCC step

Examples:
  source config.sh && ./02-build.sh all
  ./02-build.sh glibc          # Re-run one step after a failure
  FORCE=1 ./02-build.sh gcc2   # Rebuild a completed step
EOF
}

print_step_status() {
  mkdir -p "$STAMPDIR"
  local steps=(
    "1-linux-headers"
    "2-binutils"
    "3-gcc-stage1"
    "4-glibc-headers"
    "5-libgcc"
    "6-glibc"
    "7-gcc-stage2"
  )
  echo "Step status:"
  for step in "${steps[@]}"; do
    if [[ -f "$STAMPDIR/$step.done" ]]; then
      echo "  [done] $step"
    else
      echo "  [todo] $step"
    fi
  done
}

clean_build() {
  log "Cleaning build directories and stamps..."
  rm -rf "$BUILDDIR/binutils" \
         "$BUILDDIR/gcc-stage1" \
         "$BUILDDIR/glibc-headers" \
         "$BUILDDIR/glibc" \
         "$BUILDDIR/gcc-stage2" \
         "$STAMPDIR"
  log "Clean finished."
}

main() {
  local step="${1:-all}"
  mkdir -p "$BUILDDIR" "$LOGDIR" "$STAMPDIR" "$SYSROOT/usr/include" "$SYSROOT/usr/lib"
  acquire_lock

  case "$step" in
    deps)      check_deps ;;
    list)      print_step_status ;;
    clean)     clean_build ;;
    headers)   check_sources; run_step "1-linux-headers"  "1-linux-headers.done" 1 step_linux_headers ;;
    binutils)  check_sources; run_step "2-binutils"       "2-binutils.done" 2 step_binutils ;;
    gcc1)      check_sources; run_step "3-gcc-stage1"     "3-gcc-stage1.done" 3 step_gcc1 ;;
    glibc-h)   check_sources; run_step "4-glibc-headers"  "4-glibc-headers.done" 4 step_glibc_headers ;;
    libgcc)    check_sources; run_step "5-libgcc"         "5-libgcc.done" 5 step_libgcc ;;
    glibc)     check_sources; run_step "6-glibc"          "6-glibc.done" 6 step_glibc ;;
    gcc2)      check_sources; run_step "7-gcc-stage2"     "7-gcc-stage2.done" 7 step_gcc2 ;;
    all)
      check_deps
      check_sources
      run_step "1-linux-headers"  "1-linux-headers.done" 1 step_linux_headers
      run_step "2-binutils"       "2-binutils.done" 2 step_binutils
      run_step "3-gcc-stage1"     "3-gcc-stage1.done" 3 step_gcc1
      run_step "4-glibc-headers"  "4-glibc-headers.done" 4 step_glibc_headers
      run_step "5-libgcc"         "5-libgcc.done" 5 step_libgcc
      run_step "6-glibc"          "6-glibc.done" 6 step_glibc
      run_step "7-gcc-stage2"     "7-gcc-stage2.done" 7 step_gcc2
      log "Done! Toolchain installed at: $PREFIX"
      log "Add to PATH: export PATH=\"$PREFIX/bin:\$PATH\""
      ;;
    -h|--help|help) usage ;;
    *)
      echo "Unknown step: $step"; usage; exit 1 ;;
  esac
}

main "$@"
