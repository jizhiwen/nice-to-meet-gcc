#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/config.sh"

mkdir -p "$SRCDIR" "$BUILDDIR" "$LOGDIR" "$OUTPUT"

download() {
  local url="$1"
  local out="$2"
  if [[ -f "$out" ]]; then
    echo "  Already present: $(basename "$out")"
    return 0
  fi
  echo "  Downloading: $url"
  curl -fL --retry 3 --retry-delay 5 -o "$out" "$url"
}

extract() {
  local archive="$1"
  local dir="$2"
  if [[ -d "$dir" ]]; then
    echo "  Already extracted: $(basename "$dir")"
    return 0
  fi
  echo "  Extracting: $(basename "$archive")"
  case "$archive" in
    *.tar.xz) tar -xf "$archive" -C "$SRCDIR" ;;
    *.tar.gz) tar -xzf "$archive" -C "$SRCDIR" ;;
    *) tar -xf "$archive" -C "$SRCDIR" ;;
  esac
}

echo "=== Downloading sources ==="

download "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${LINUX_VER}.tar.xz" \
         "$SRCDIR/linux-${LINUX_VER}.tar.xz"
download "https://ftp.gnu.org/gnu/bash/bash-${BASH_VER}.tar.gz" \
         "$SRCDIR/bash-${BASH_VER}.tar.gz"

echo "=== Extracting ==="
extract "$SRCDIR/linux-${LINUX_VER}.tar.xz" "$KERNEL_SRC"
extract "$SRCDIR/bash-${BASH_VER}.tar.gz" "$BASH_SRC"

echo "=== Downloading busybox ${BUSYBOX_VER} ==="
download "https://busybox.net/downloads/busybox-${BUSYBOX_VER}.tar.bz2" \
         "$SRCDIR/busybox-${BUSYBOX_VER}.tar.bz2"
if [[ -d "$BUSYBOX_SRC" ]]; then
  echo "  Already extracted: busybox-${BUSYBOX_VER}"
else
  echo "  Extracting: busybox-${BUSYBOX_VER}.tar.bz2"
  tar -xjf "$SRCDIR/busybox-${BUSYBOX_VER}.tar.bz2" -C "$SRCDIR"
fi

echo "=== Download complete ==="
echo "Kernel:  $KERNEL_SRC"
echo "Bash:    $BASH_SRC"
echo "Busybox: $BUSYBOX_SRC"
