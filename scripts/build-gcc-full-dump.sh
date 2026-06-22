#!/usr/bin/env bash
# Build a local GCC 11.4 with full tree-original raw dumps (no TDF_SLIM).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/gcc-11.4.0"
BUILD="$SRC/build-full-dump"
PREFIX="$ROOT/install/gcc-11.4-full-dump"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

if [[ ! -f "$SRC/configure" ]]; then
  echo "error: GCC source not found at $SRC" >&2
  exit 1
fi

mkdir -p "$BUILD"
cd "$BUILD"

if [[ ! -f Makefile ]]; then
  echo "==> configuring GCC (prefix: $PREFIX)"
  "$SRC/configure" \
    --prefix="$PREFIX" \
    --enable-languages=c \
    --disable-multilib \
    --disable-bootstrap
fi

echo "==> building all-gcc (jobs=$JOBS)"
make -j"$JOBS" all-gcc

echo
echo "Built: $BUILD/gcc/xgcc"
echo "Install (optional): make install"
echo "Prefix would be: $PREFIX"
