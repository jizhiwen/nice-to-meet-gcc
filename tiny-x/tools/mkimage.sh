#!/bin/bash
#
# mkimage.sh - Create bootable ISO for VirtualBox
#
# Uses grub-mkrescue to create a hybrid ISO that boots in VirtualBox.
# The ISO contains:
#   /boot/kernel       - our kernel binary
#   /boot/grub/grub.cfg
#   /boot/initramfs.cpio
#
# Usage: ./mkimage.sh
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$ROOT_DIR/build"
ISO_DIR="$BUILD_DIR/iso_root"
OUTPUT_ISO="$BUILD_DIR/tiny-x.iso"

echo "=== Building tiny-x bootable ISO ==="

# Create ISO directory structure
mkdir -p "$ISO_DIR/boot/grub"

# Copy kernel
if [ ! -f "$BUILD_DIR/kernel/kernel" ]; then
    echo "ERROR: kernel not found at $BUILD_DIR/kernel/kernel"
    echo "Run 'make' first to build the kernel."
    exit 1
fi

cp "$BUILD_DIR/kernel/kernel"  "$ISO_DIR/boot/kernel"
cp "$ROOT_DIR/grub/grub.cfg"   "$ISO_DIR/boot/grub/grub.cfg"

# Build and copy initramfs
"$SCRIPT_DIR/mkinitramfs.sh" "$BUILD_DIR" "$ISO_DIR/boot/initramfs.cpio"

# Create ISO using grub-mkrescue
echo "Creating bootable ISO..."
grub-mkrescue \
    --install-modules="multiboot2 normal boot iso9660 fat ext2 configfile" \
    --output="$OUTPUT_ISO" \
    "$ISO_DIR" \
    2>&1 | tail -5

echo ""
echo "=== ISO created: $OUTPUT_ISO ==="
echo ""
echo "To boot in VirtualBox:"
echo "  1. Create a new VM (Type: Linux, Version: Other Linux 64-bit)"
echo "  2. Set memory to 256MB minimum"
echo "  3. Under Storage: attach $OUTPUT_ISO as IDE CD-ROM"
echo "  4. Start the VM"
echo ""
echo "To boot with QEMU (if available):"
echo "  qemu-system-x86_64 -cdrom $OUTPUT_ISO -m 256M -serial stdio"
