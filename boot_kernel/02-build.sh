#!/usr/bin/env bash
# =============================================================================
# boot_kernel — Linux kernel + initramfs (static bash + busybox)
#
# Steps:
#   1. kernel    →  bzImage
#   2. bash      →  static bash (interactive shell)
#   3. busybox   →  static coreutils (mount, ls, …)
#   4. initramfs →  cpio.gz root filesystem in RAM
# =============================================================================
set -Eeuo pipefail

source "$(dirname "$0")/config.sh"

log() { echo "[$(date '+%H:%M:%S')] $*"; }

export STAMPDIR="$BUILDDIR/.stamps"
export LOCKFILE="$BUILDDIR/.build.lock"
LAST_LOG=""

on_error() {
  local exit_code=$?
  echo
  log "Build failed (exit code: $exit_code)."
  [[ -n "$LAST_LOG" ]] && log "Log: $LAST_LOG"
  exit "$exit_code"
}
trap on_error ERR

acquire_lock() {
  exec 9>"$LOCKFILE"
  if ! flock -n 9; then
    echo "Another boot_kernel build is already running."
    exit 1
  fi
}

invalidate_later_stamps() {
  local current="$1"
  case "$current" in
    1) rm -f "$STAMPDIR"/2-* "$STAMPDIR"/3-* "$STAMPDIR"/4-* ;;
    2) rm -f "$STAMPDIR"/3-* "$STAMPDIR"/4-* ;;
    3) rm -f "$STAMPDIR"/4-* ;;
    4) ;;
  esac
}

step_initramfs_tiny() {
  step_initramfs 1
}

run_step() {
  local name="$1"
  local stamp="$2"
  local step_no="$3"
  shift 3
  local logfile="$LOGDIR/${name}.log"
  LAST_LOG="$logfile"
  if [[ -f "$STAMPDIR/$stamp" && "${FORCE:-0}" != "1" ]]; then
    log ">>> $name (skipped)"
    return 0
  fi
  log ">>> $name"
  "$@" 2>&1 | tee "$logfile"
  touch "$STAMPDIR/$stamp"
  invalidate_later_stamps "$step_no"
  log "<<< $name done"
}

check_deps() {
  local missing=()
  for cmd in gcc make curl cpio gzip file flock; do
    command -v "$cmd" >/dev/null 2>&1 || missing+=("$cmd")
  done
  if ((${#missing[@]})); then
    echo "Missing: ${missing[*]}"
    echo "Ubuntu: sudo apt install build-essential curl cpio qemu-system-x86 libssl-dev"
    exit 1
  fi
}

check_kernel_source() {
  [[ -d "$KERNEL_SRC" ]] || { echo "Run: ./01-download.sh (kernel)"; exit 1; }
}

check_bash_source() {
  [[ -d "$BASH_SRC" ]] || { echo "Run: ./01-download.sh (bash)"; exit 1; }
}

check_busybox_source() {
  [[ -d "$BUSYBOX_SRC" ]] || { echo "Run: ./01-download.sh (busybox)"; exit 1; }
}

step_kernel() {
  if [[ ! -f /usr/include/openssl/bio.h ]]; then
    echo "Kernel build needs OpenSSL headers on the host (not packaged into initramfs)."
    echo "Ubuntu: sudo apt install libssl-dev"
    exit 1
  fi

  mkdir -p "$KERNEL_BUILD" "$OUTPUT"
  cd "$KERNEL_BUILD"

  make -C "$KERNEL_SRC" O="$KERNEL_BUILD" ARCH="$ARCH" defconfig
  "$KERNEL_SRC/scripts/kconfig/merge_config.sh" -m -O "$KERNEL_BUILD" \
    "$KERNEL_BUILD/.config" "$TOP/configs/boot.config"
  make -C "$KERNEL_SRC" O="$KERNEL_BUILD" ARCH="$ARCH" olddefconfig

  "$KERNEL_SRC/scripts/config" --file "$KERNEL_BUILD/.config" \
    --disable SYSTEM_TRUSTED_KEYRING \
    --disable SYSTEM_BLACKLIST_KEYRING \
    --disable SYSTEM_REVOCATION_LIST
  make -C "$KERNEL_SRC" O="$KERNEL_BUILD" ARCH="$ARCH" olddefconfig

  make -C "$KERNEL_SRC" O="$KERNEL_BUILD" ARCH="$ARCH" -j"$JOBS" bzImage
  cp -f "$KERNEL_BUILD/arch/x86/boot/bzImage" "$OUTPUT/bzImage"
  log "Kernel: $OUTPUT/bzImage"
}

step_bash() {
  rm -rf "$BASH_BUILD" "$BASH_INSTALL"
  mkdir -p "$BASH_BUILD"
  cd "$BASH_BUILD"

  "$BASH_SRC/configure" \
    --prefix="$BASH_INSTALL" \
    --enable-static-link \
    --disable-readline \
    --without-bash-malloc

  make -j"$JOBS" LDFLAGS=-static
  make install

  if ! file "$BASH_BIN" | grep -q 'statically linked'; then
    echo "Expected statically linked bash at $BASH_BIN"
    file "$BASH_BIN"
    exit 1
  fi
  log "Bash: $BASH_BIN ($(du -h "$BASH_BIN" | awk '{print $1}'), static)"
}

step_busybox() {
  rm -rf "$BUSYBOX_BUILD"
  mkdir -p "$BUSYBOX_BUILD"

  case "$BUSYBOX_PROFILE" in
    mini|minimal)
      make -C "$BUSYBOX_SRC" O="$BUSYBOX_BUILD" allnoconfig
      sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' "$BUSYBOX_BUILD/.config"
      sed -i 's/CONFIG_SH_IS_ASH=y/# CONFIG_SH_IS_ASH is not set/' "$BUSYBOX_BUILD/.config"
      sed -i 's/# CONFIG_SH_IS_NONE is not set/CONFIG_SH_IS_NONE=y/' "$BUSYBOX_BUILD/.config"
      while IFS= read -r applet || [[ -n "$applet" ]]; do
        [[ -z "$applet" || "$applet" =~ ^# ]] && continue
        sed -i "s/# CONFIG_${applet} is not set/CONFIG_${applet}=y/" "$BUSYBOX_BUILD/.config"
      done < "$TOP/configs/busybox-minimal.applets"
      log "Busybox profile: mini ($(grep -cv '^#\|^$' "$TOP/configs/busybox-minimal.applets") applets)"
      ;;
    full)
      make -C "$BUSYBOX_SRC" O="$BUSYBOX_BUILD" defconfig
      sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' "$BUSYBOX_BUILD/.config"
      log "Busybox profile: full (defconfig)"
      ;;
    *)
      echo "Unknown busybox profile: $BUSYBOX_PROFILE (use mini or full)"
      exit 1
      ;;
  esac

  make -C "$BUSYBOX_SRC" O="$BUSYBOX_BUILD" -j"$JOBS"

  if ! file "$BUSYBOX_BUILD/busybox" | grep -q 'statically linked'; then
    echo "Expected statically linked busybox"
    file "$BUSYBOX_BUILD/busybox"
    exit 1
  fi
  log "Busybox: $BUSYBOX_BUILD/busybox ($(du -h "$BUSYBOX_BUILD/busybox" | awk '{print $1}'), static, $BUSYBOX_PROFILE)"
}

step_tinysh() {
  mkdir -p "$BUILDDIR/tinysh"
  gcc -nostdlib -static -fno-pie -fno-builtin -fno-stack-protector \
    -Wall -Wextra -O2 -Wl,-z,noexecstack \
    "$TOP/shell/crt0.S" "$TOP/shell/tiny_shell.c" \
    -o "$TINYSH"

  if file "$TINYSH" | grep -q 'dynamically linked'; then
    echo "Expected fully static tinysh (no libc)"
    file "$TINYSH"
    exit 1
  fi
  log "Tinysh: $TINYSH ($(du -h "$TINYSH" | awk '{print $1}'), no libc)"
}

step_initramfs() {
  local use_tinysh="${1:-0}"
  [[ -x "$BUSYBOX_BUILD/busybox" ]] || { echo "Run: ./02-build.sh busybox"; exit 1; }

  if [[ "$use_tinysh" == "1" ]]; then
    [[ -x "$TINYSH" ]] || { echo "Run: ./02-build.sh tinysh"; exit 1; }
  else
    [[ -x "$BASH_BIN" ]] || { echo "Run: ./02-build.sh bash"; exit 1; }
  fi

  rm -rf "$ROOTFS"
  mkdir -p "$ROOTFS"/{bin,dev,proc,sys,tmp,root}

  make -C "$BUSYBOX_SRC" O="$BUSYBOX_BUILD" CONFIG_PREFIX="$ROOTFS" install

  if [[ "$use_tinysh" == "1" ]]; then
    install -D -m 755 "$TINYSH" "$ROOTFS/bin/tinysh"
  else
    install -D -m 755 "$BASH_BIN" "$ROOTFS/bin/bash"
    install -D -m 755 "$TOP/rootfs/init" "$ROOTFS/init"
  fi

  mkdir -p "$OUTPUT"
  local out="$INITRAMFS"
  [[ "$use_tinysh" == "1" ]] && out="$INITRAMFS_TINY"
  (
    cd "$ROOTFS"
    find . -print0 | cpio --null -ov --format=newc
  ) | gzip -9 > "$out"

  log "Initramfs: $out ($(du -h "$out" | awk '{print $1}'))"
}

usage() {
  cat <<EOF
Usage: $0 [step]

Steps:
  all              Build everything, busybox mini (default)
  all-full         Build everything, busybox full
  all-tiny         kernel + tinysh + busybox mini + initramfs-tiny (no bash)
  deps             Check host tools
  kernel           1. bzImage
  bash             2. static bash
  tinysh           2b. tiny shell (no libc, replaces bash)
  busybox          3. static busybox mini (default)
  busybox-mini     3. static busybox mini (key applets only)
  busybox-full     3. static busybox full (defconfig)
  initramfs        4. pack cpio.gz (bash + busybox)
  initramfs-tiny   4. pack cpio.gz (tinysh + busybox, no bash)
  list             Step status
  clean            Remove build output

Busybox mini applets: configs/busybox-minimal.applets

Environment: LINUX_VER  BASH_VER  BUSYBOX_VER  JOBS  FORCE=1
             BUSYBOX_PROFILE=mini|full  (overridden by step name)
EOF
}

set_busybox_profile() {
  case "$1" in
    mini|minimal) export BUSYBOX_PROFILE=mini ;;
    full)         export BUSYBOX_PROFILE=full ;;
    *)
      echo "Unknown busybox profile: $1 (use mini or full)"
      exit 1
      ;;
  esac
}

run_busybox_step() {
  set_busybox_profile "${1:-mini}"
  check_busybox_source
  run_step "3-busybox-${BUSYBOX_PROFILE}" "3-busybox-${BUSYBOX_PROFILE}.done" 3 step_busybox
}

run_all() {
  local bb_profile="${1:-mini}"
  check_deps
  check_kernel_source
  check_bash_source
  check_busybox_source
  run_step "1-kernel" "1-kernel.done" 1 step_kernel
  run_step "2-bash" "2-bash.done" 2 step_bash
  run_busybox_step "$bb_profile"
  run_step "4-initramfs" "4-initramfs.done" 4 step_initramfs
  log "Done. Run: ./03-run.sh"
}

run_all_tiny() {
  local bb_profile="${1:-mini}"
  check_deps
  check_kernel_source
  check_busybox_source
  run_step "1-kernel" "1-kernel.done" 1 step_kernel
  run_step "2-tinysh" "2-tinysh.done" 2 step_tinysh
  run_busybox_step "$bb_profile"
  run_step "4-initramfs-tiny" "4-initramfs-tiny.done" 4 step_initramfs_tiny
  log "Done. Run: ./03-run.sh tiny"
}

print_step_status() {
  mkdir -p "$STAMPDIR"
  for step in 1-kernel 2-bash 2-tinysh 3-busybox-mini 3-busybox-full 4-initramfs 4-initramfs-tiny; do
    if [[ -f "$STAMPDIR/$step.done" ]]; then
      echo "  [done] $step"
    else
      echo "  [todo] $step"
    fi
  done
}

clean_build() {
  rm -rf "$BUILDDIR" "$OUTPUT"
  log "Clean finished."
}

main() {
  local step="${1:-all}"
  mkdir -p "$BUILDDIR" "$LOGDIR" "$STAMPDIR" "$OUTPUT"
  acquire_lock

  case "$step" in
    deps)           check_deps ;;
    list)           print_step_status ;;
    clean)          clean_build ;;
    kernel)         check_kernel_source; run_step "1-kernel" "1-kernel.done" 1 step_kernel ;;
    bash)           check_bash_source; run_step "2-bash" "2-bash.done" 2 step_bash ;;
    tinysh)         run_step "2-tinysh" "2-tinysh.done" 2 step_tinysh ;;
    busybox|busybox-mini) run_busybox_step mini ;;
    busybox-full)   run_busybox_step full ;;
    initramfs)      run_step "4-initramfs" "4-initramfs.done" 4 step_initramfs ;;
    initramfs-tiny) run_step "4-initramfs-tiny" "4-initramfs-tiny.done" 4 step_initramfs_tiny ;;
    all)            run_all mini ;;
    all-full)       run_all full ;;
    all-tiny)       run_all_tiny mini ;;
    -h|--help|help) usage ;;
    *) echo "Unknown step: $step"; usage; exit 1 ;;
  esac
}

main "$@"
