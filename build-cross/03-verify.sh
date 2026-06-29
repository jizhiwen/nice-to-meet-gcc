#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/config.sh"

CC="${TARGET}-gcc"
CXX="${TARGET}-g++"

echo "=== 验证交叉工具链 ==="
echo "PREFIX:  $PREFIX"
echo "TARGET:  $TARGET"
echo "SYSROOT: $SYSROOT"
echo

for tool in gcc g++ ld as ar ranlib strip objcopy; do
  path="$PREFIX/bin/${TARGET}-${tool}"
  if [[ -x "$path" ]]; then
    echo "[OK] $path"
  else
    echo "[FAIL] 缺少 $path"
    exit 1
  fi
done

echo
echo "--- 版本信息 ---"
"$CC" --version | head -1
"$CC" -dumpmachine

echo
echo "--- 编译测试程序 ---"
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

cat > "$TMPDIR/hello.c" <<'EOF'
#include <stdio.h>
int main(void) {
    printf("Hello from aarch64 cross compiler!\n");
    return 0;
}
EOF

cat > "$TMPDIR/test.cpp" <<'EOF'
#include <iostream>
int main() {
    std::cout << "C++ OK" << std::endl;
    return 0;
}
EOF

# 静态链接测试（不依赖目标机动态加载器）
"$CC" -static -o "$TMPDIR/hello" "$TMPDIR/hello.c"
file "$TMPDIR/hello"
"$CXX" -static -o "$TMPDIR/test" "$TMPDIR/test.cpp"
file "$TMPDIR/test"

echo
echo "--- 检查 sysroot ---"
ls "$SYSROOT/usr/lib/libc.so"* 2>/dev/null || echo "警告: 未找到 libc"
ls "$SYSROOT/usr/include/stdio.h" 2>/dev/null || echo "警告: 未找到 stdio.h"

echo
echo "=== 验证通过 ==="
echo "使用示例:"
echo "  ${TARGET}-gcc -o hello hello.c"
echo "  ${TARGET}-gcc -static -o hello_static hello.c"
