#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/config.sh"

mode="${1:-bash}"
shift || true

case "$mode" in
  bash|"") ;;
  tiny)
    INITRAMFS="$INITRAMFS_TINY"
    QEMU_APPEND="$QEMU_APPEND_TINY"
    ;;
  -h|--help|help)
    cat <<EOF
Usage: $0 [bash|tiny] [extra qemu args...]

  bash  (default)  initramfs with static bash — rdinit=/init
  tiny             initramfs with tinysh (no libc) — rdinit=/bin/tinysh

Build first:
  ./02-build.sh all        # bash variant
  ./02-build.sh all-tiny   # tiny variant
EOF
    exit 0
    ;;
  *)
    # Treat first arg as extra QEMU option if not a known mode
    set -- "$mode" "$@"
    mode=bash
    ;;
esac

RUN_INFO="$OUTPUT/last-qemu-run.txt"

if [[ ! -f "$BZIMAGE" || ! -f "$INITRAMFS" ]]; then
  echo "Missing kernel or initramfs. Build first:"
  if [[ "$mode" == "tiny" ]]; then
    echo "  ./02-build.sh all-tiny"
  else
    echo "  ./02-build.sh all"
  fi
  exit 1
fi

if ! command -v "$QEMU" >/dev/null 2>&1; then
  echo "QEMU not found: $QEMU"
  echo "Ubuntu: sudo apt install qemu-system-x86"
  exit 1
fi

mkdir -p "$OUTPUT"
{
  echo "=== boot_kernel QEMU ($mode) ==="
  echo "Time:     $(date -Iseconds)"
  echo "Kernel:   $BZIMAGE"
  echo "Initramfs: $INITRAMFS"
  echo "Append:   $QEMU_APPEND"
  echo "Memory:   $QEMU_MEM"
  echo "Cmd:      $QEMU -m $QEMU_MEM -kernel $BZIMAGE -initrd $INITRAMFS -append \"$QEMU_APPEND\" ..."
} > "$RUN_INFO"

info() { echo "$*" >&2; }

info "=== boot_kernel QEMU ($mode) ==="
info "Kernel:    $BZIMAGE"
info "Initramfs: $INITRAMFS"
info "Append:    $QEMU_APPEND"
info "Saved to:  $RUN_INFO"
info
info "Kernel boot log follows (banner scrolls away — that is normal)."
info "Exit QEMU: Ctrl-A then X"
info

set +e
"$QEMU" \
  -m "$QEMU_MEM" \
  -kernel "$BZIMAGE" \
  -initrd "$INITRAMFS" \
  -append "$QEMU_APPEND" \
  -nographic \
  -no-reboot \
  -serial mon:stdio \
  "$@"
rc=$?
set -e

info
info "QEMU exited (code $rc). Launch params: $RUN_INFO"
exit "$rc"
