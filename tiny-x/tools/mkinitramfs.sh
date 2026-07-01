#!/bin/bash
#
# mkinitramfs.sh - Build initramfs cpio archive
#
# Creates a newc-format cpio archive containing:
#   /bin/sh         - tiny shell
#   /bin/hello      - hello world
#   /lib/libc.so    - shared C library
#   /lib/ld-tiny.so - dynamic linker
#   /etc/motd       - message of the day
#
# Usage: ./mkinitramfs.sh <build_dir> <output.cpio>
#

set -e

BUILD_DIR="${1:-../build}"
OUTPUT="${2:-../build/initramfs.cpio}"

echo "Building initramfs..."

# Create staging directory
STAGE=$(mktemp -d)
trap "rm -rf $STAGE" EXIT

mkdir -p "$STAGE/bin"
mkdir -p "$STAGE/lib"
mkdir -p "$STAGE/etc"
mkdir -p "$STAGE/tmp"
mkdir -p "$STAGE/proc"
mkdir -p "$STAGE/dev"

# Copy binaries
[ -f "$BUILD_DIR/shell/sh" ]        && cp "$BUILD_DIR/shell/sh"       "$STAGE/bin/sh"
[ -f "$BUILD_DIR/hello/hello" ]     && cp "$BUILD_DIR/hello/hello"    "$STAGE/bin/hello"

# Copy libraries
[ -f "$BUILD_DIR/libc/libc.so" ]    && cp "$BUILD_DIR/libc/libc.so"   "$STAGE/lib/libc.so"
[ -f "$BUILD_DIR/dynlink/ld-tiny.so" ] && cp "$BUILD_DIR/dynlink/ld-tiny.so" "$STAGE/lib/ld-tiny.so"

# Message of the day
cat > "$STAGE/etc/motd" << 'EOF'
Welcome to tiny-x OS!
A minimal educational OS implementing Linux+GCC+libc+binutils concepts.

Components:
  - Custom x86_64 kernel (Multiboot2)
  - Tiny shell (/bin/sh)
  - Dynamic linker (/lib/ld-tiny.so)
  - Minimal libc (/lib/libc.so)

Type 'hello' to run the Hello World program.
Type 'help' for shell commands.
EOF

# Create cpio archive
cd "$STAGE"
find . | sort | cpio --create --format=newc 2>/dev/null > "$OUTPUT"
cd -

echo "initramfs created: $OUTPUT ($(du -h $OUTPUT | cut -f1))"
echo "Contents:"
cpio -t < "$OUTPUT" 2>/dev/null | head -20
