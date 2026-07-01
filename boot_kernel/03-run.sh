#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/config.sh"

RUN_INFO="$OUTPUT/last-qemu-run.txt"

if [[ ! -f "$BZIMAGE" || ! -f "$INITRAMFS" ]]; then
  echo "Missing kernel or initramfs. Build first:"
  echo "  ./02-build.sh all"
  exit 1
fi

if ! command -v "$QEMU" >/dev/null 2>&1; then
  echo "QEMU not found: $QEMU"
  echo "Ubuntu: sudo apt install qemu-system-x86"
  exit 1
fi

mkdir -p "$OUTPUT"
{
  echo "=== boot_kernel QEMU ==="
  echo "Time:     $(date -Iseconds)"
  echo "Kernel:   $BZIMAGE"
  echo "Initramfs: $INITRAMFS"
  echo "Append:   $QEMU_APPEND"
  echo "Memory:   $QEMU_MEM"
  echo "Cmd:      $QEMU -m $QEMU_MEM -kernel $BZIMAGE -initrd $INITRAMFS -append \"$QEMU_APPEND\" ..."
} > "$RUN_INFO"

# stderr survives in scrollback slightly better; full copy always in last-qemu-run.txt
info() { echo "$*" >&2; }

info "=== boot_kernel QEMU ==="
info "Kernel:    $BZIMAGE"
info "Initramfs: $INITRAMFS"
info "Append:    $QEMU_APPEND"
info "Saved to:  $RUN_INFO"
info
info "Kernel boot log follows (banner scrolls away — that is normal)."
info "Exit QEMU: Ctrl-A then X"
info

# Do not use exec: print a footer after QEMU exits.
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
