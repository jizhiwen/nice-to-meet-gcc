#!/usr/bin/env bash
# =============================================================================
# Native Linux toolchain — step-by-step build script
#
# Same component versions as build-cross, but host == target (native build).
# GCC and Glibc still depend on each other, so we bootstrap in stages:
#
# Order:
#   1. Linux headers   →  linux/*.h under PREFIX
#   2. Binutils          →  as, ld, ...
#   3. GCC stage 1       →  minimal C compiler (no libc yet)
#   4. Glibc headers     →  crt*.o, headers, stub libc.so
#   5. libgcc            →  threads / runtime support
#   6. Full Glibc        →  real libc.so
#   7. GCC stage 2       →  full gcc/g++ with shared libs
# =============================================================================
set -Eeuo pipefail

source "$(dirname "$0")/config.sh"

log() { echo "[$(date '+%H:%M:%S')] $*"; }

# Strip PREFIX/bin so host tools (as/ld) from a prior build are not used while
# rebuilding early steps. Bootstrap ld defaults to PIE and breaks gprofng etc.
host_path() {
  local p=":$PATH:"
  p="${p//:$PREFIX\/bin:/:}"
  p="${p#:}"
  p="${p%:}"
  echo "$p"
}

# Remove a failed/partial gcc install; keep binutils (as, ld, ar, ...) in PREFIX.
clean_partial_gcc() {
  rm -rf "$PREFIX/lib/gcc" "$PREFIX/libexec/gcc" "$PREFIX/$BUILD"
  local tool
  for tool in gcc g++ cpp gcov gcc-ar gcc-nm gcc-ranlib \
              xgcc xg++ c++ gfortran gccgo gdc gm2; do
    rm -f "$PREFIX/bin/$tool"
  done
}

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
    echo "Another build-native job is already running."
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

step_linux_headers() {
  mkdir -p "$SYSROOT/usr"
  make -C "$SRCDIR/linux-${LINUX_VER}" \
    ARCH="$LINUX_ARCH" \
    INSTALL_HDR_PATH="$SYSROOT/usr" \
    headers_install
}

step_binutils() {
  local dir="$BUILDDIR/binutils"
  local build_path
  build_path="$(host_path)"
  rm -rf "$dir" && mkdir -p "$dir" && cd "$dir"

  PATH="$build_path" \
  "$SRCDIR/binutils-${BINUTILS_VER}/configure" \
    --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" \
    --disable-multilib \
    --disable-nls \
    --disable-werror

  PATH="$build_path" make -j"$JOBS"
  PATH="$build_path" make install
}

step_gcc1() {
  local dir="$BUILDDIR/gcc-stage1"
  local build_path
  build_path="$(host_path)"
  rm -rf "$dir" && mkdir -p "$dir" && cd "$dir"

  clean_partial_gcc
  rm -f "$SYSROOT/usr/lib/libc.so" "$SYSROOT/usr/lib64/libc.so"

  if [[ -f "$STAMPDIR/4-glibc-headers.done" ]]; then
    log "WARNING: glibc headers already installed; gcc1 expects Linux headers only."
    log "         Prefer: ./02-build.sh clean && ./02-build.sh all"
  fi

  # Build host-side deps with the system compiler/linker; target as/ld from step 2.
  PATH="$build_path" \
  CC="${CC:-gcc}" \
  CXX="${CXX:-g++}" \
  "$SRCDIR/gcc-${GCC_VER}/configure" \
    --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" \
    --with-as="$PREFIX/bin/as" \
    --with-ld="$PREFIX/bin/ld" \
    --with-newlib \
    --without-headers \
    --enable-languages=c \
    --disable-shared \
    --disable-threads \
    --disable-nls \
    --disable-bootstrap \
    --disable-multilib \
    --with-system-zlib \
    --disable-werror

  PATH="$build_path" make -j"$JOBS" all-gcc
  PATH="$build_path" make install-gcc
}

step_glibc_headers() {
  local dir="$BUILDDIR/glibc-headers"
  rm -rf "$dir" && mkdir -p "$dir" && cd "$dir"

  CC="$PREFIX/bin/gcc" \
  libc_cv_cxx_link_ok=no \
  "$SRCDIR/glibc-${GLIBC_VER}/configure" \
    --prefix=/usr \
    --host="$BUILD" \
    --build="$BUILD" \
    --with-headers="$SYSROOT/usr/include" \
    --disable-werror \
    --enable-kernel=4.19

  make -j"$JOBS" install-bootstrap-headers=yes install-headers DESTDIR="$SYSROOT"
  make -j"$JOBS" csu/subdir_lib
  install -D csu/crt1.o "$SYSROOT/usr/lib/crt1.o"
  install -D csu/crti.o "$SYSROOT/usr/lib/crti.o"
  install -D csu/crtn.o "$SYSROOT/usr/lib/crtn.o"

  "$PREFIX/bin/gcc" -nostdlib -nostartfiles -shared -x c /dev/null \
    -o "$SYSROOT/usr/lib/libc.so"
  touch "$SYSROOT/usr/include/gnu/stubs.h"
}

step_libgcc() {
  cd "$BUILDDIR/gcc-stage1"
  make -j"$JOBS" all-target-libgcc
  make install-target-libgcc
}

step_glibc() {
  local dir="$BUILDDIR/glibc"
  rm -rf "$dir" && mkdir -p "$dir" && cd "$dir"

  CC="$PREFIX/bin/gcc" \
  libc_cv_cxx_link_ok=no \
  "$SRCDIR/glibc-${GLIBC_VER}/configure" \
    --prefix=/usr \
    --host="$BUILD" \
    --build="$BUILD" \
    --with-headers="$SYSROOT/usr/include" \
    --disable-werror \
    --enable-kernel=4.19 \
    --with-default-link

  make -j"$JOBS"
  make DESTDIR="$SYSROOT" install

  # Step 4 leaves a stub libc.so in usr/lib for gcc link tests. Full glibc
  # installs the real libc under lib64/usr/lib64 on x86_64; the stub must go.
  rm -f "$SYSROOT/usr/lib/libc.so"
}

step_gcc2() {
  local dir="$BUILDDIR/gcc-stage2"
  local gcc_jobs="${JOBS_GCC2:-$JOBS}"
  local build_path
  build_path="$(host_path)"
  rm -rf "$dir" && mkdir -p "$dir" && cd "$dir"

  rm -f "$SYSROOT/usr/lib/libc.so"

  # Glibc 2.40 headers in --with-sysroot redirect strtoul -> __isoc23_strtoul for
  # host libiberty, but host tools must link/run with the system libc. Undefine the
  # redirect for host-side compiles. ISL needs -fPIC/-no-pie with bootstrap ld.
  local host_cflags="-g -O2 -fPIC -Dstrtoul=strtoul -Dstrtol=strtol -Dstrtoull=strtoull"
  PATH="$build_path" \
  CC="${CC:-gcc}" \
  CXX="${CXX:-g++}" \
  CFLAGS="${CFLAGS:-$host_cflags}" \
  CXXFLAGS="${CXXFLAGS:-$host_cflags}" \
  "$SRCDIR/gcc-${GCC_VER}/configure" \
    --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" \
    --with-as="$PREFIX/bin/as" \
    --with-ld="$PREFIX/bin/ld" \
    --enable-languages=c,c++ \
    --disable-nls \
    --disable-bootstrap \
    --disable-multilib \
    --with-system-zlib \
    --disable-werror \
    --enable-default-pie=no \
    --with-stage1-ldflags="-static-libstdc++ -static-libgcc -no-pie"

  PATH="$build_path" make -j"$gcc_jobs"
  PATH="$build_path" make install
}

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

Environment: PREFIX  BUILD  JOBS  FORCE=1  (see config.sh)
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
      log "Run programs with: export LD_LIBRARY_PATH=\"$PREFIX/usr/lib:$PREFIX/lib:\$LD_LIBRARY_PATH\""
      ;;
    -h|--help|help) usage ;;
    *)
      echo "Unknown step: $step"; usage; exit 1 ;;
  esac
}

main "$@"
