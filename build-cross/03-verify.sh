#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/config.sh"

CC="${TARGET}-gcc"
CXX="${TARGET}-g++"

echo "=== Verifying cross-toolchain ==="
echo "PREFIX:  $PREFIX"
echo "TARGET:  $TARGET"
echo "SYSROOT: $SYSROOT"
echo

for tool in gcc ld as ar ranlib strip objcopy; do
  path="$PREFIX/bin/${TARGET}-${tool}"
  if [[ -x "$path" ]]; then
    echo "[OK] $path"
  else
    echo "[FAIL] Missing $path"
    exit 1
  fi
done

if [[ -x "$PREFIX/bin/${TARGET}-g++" ]]; then
  echo "[OK] $PREFIX/bin/${TARGET}-g++"
  TOOLCHAIN_STAGE="full"
else
  echo "[INFO] ${TARGET}-g++ is missing."
  echo "       Current stage is likely GCC stage 1 only."
  echo "       Build the final compiler with: ./02-build.sh gcc2"
  TOOLCHAIN_STAGE="stage1"
fi

echo
echo "--- Version ---"
"$CC" --version | sed -n '1p'
"$CC" -dumpmachine

echo
echo "--- Compile test programs ---"
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

cat > "$TMPDIR/hello.c" <<'EOF'
#include <stdio.h>
int main(void) {
    printf("Hello from aarch64 cross compiler!\n");
    return 0;
}
EOF

# Always validate C linking against the target sysroot.
"$CC" --sysroot="$SYSROOT" -o "$TMPDIR/hello" "$TMPDIR/hello.c"
file "$TMPDIR/hello"

if [[ "$TOOLCHAIN_STAGE" == "full" ]]; then
  cat > "$TMPDIR/test.cpp" <<'EOF'
#include <iostream>
int main() {
    std::cout << "C++ OK" << std::endl;
    return 0;
}
EOF
  "$CXX" --sysroot="$SYSROOT" -o "$TMPDIR/test" "$TMPDIR/test.cpp"
  file "$TMPDIR/test"
fi

echo
echo "--- Check sysroot ---"
ls "$SYSROOT/usr/lib/libc.so"* 2>/dev/null || echo "Warning: libc not found"
ls "$SYSROOT/usr/include/stdio.h" 2>/dev/null || echo "Warning: stdio.h not found"

echo
if [[ "$TOOLCHAIN_STAGE" == "full" ]]; then
  echo "=== Verification passed (full C/C++ toolchain) ==="
  echo "Examples:"
  echo "  ${TARGET}-gcc --sysroot=$SYSROOT -o hello hello.c"
  echo "  ${TARGET}-g++ --sysroot=$SYSROOT -o app app.cpp"
else
  echo "=== Verification passed (stage 1 C-only toolchain) ==="
  echo "Next:"
  echo "  ./02-build.sh gcc2"
  echo "  ./03-verify.sh"
fi
