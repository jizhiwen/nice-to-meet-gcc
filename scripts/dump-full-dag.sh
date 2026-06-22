#!/usr/bin/env bash
# Dump full GENERIC tree and render DAG using the patched local GCC.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/gcc-11.4.0"
BUILD="$SRC/build-full-dump"
GCC="$BUILD/gcc/xgcc"
VIS="$ROOT/visualize_tree_dag.py"

SOURCE="${1:-$ROOT/test.c}"
if [[ ! -f "$SOURCE" ]]; then
  echo "usage: $0 [source.c]" >&2
  exit 1
fi

if [[ ! -x "$GCC" ]]; then
  echo "error: patched gcc not built yet. Run: $ROOT/scripts/build-gcc-full-dump.sh" >&2
  exit 1
fi

SRC_DIR="$(cd "$(dirname "$SOURCE")" && pwd)"
SRC_BASE="$(basename "$SOURCE")"
cd "$SRC_DIR"

echo "==> dumping with patched gcc: $GCC"
"$GCC" -B"$BUILD/gcc/" -fdump-tree-original-raw -c "$SRC_BASE" -o /dev/null

DUMP="$(ls -1 "$SRC_BASE".0*t.original | sort | tail -1)"
echo "==> dump file: $DUMP"

python3 "$VIS" --no-infer-missing "$DUMP"

echo
echo "Open: ${DUMP%.original}.html"
echo "Tip: inferred edges disabled; all links should come from GCC dump itself."
