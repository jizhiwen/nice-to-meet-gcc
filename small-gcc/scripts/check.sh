#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_DIR="$ROOT/build/bin"
EXPECTED_DIR="$ROOT/tests/expected"

out="$($BIN_DIR/hello)"
expected="$(<"$EXPECTED_DIR/hello.txt")"
if [[ "$out" != "$expected" ]]; then
  echo "FAIL: hello output"
  echo "Expected: $expected"
  echo "Got: $out"
  exit 1
fi
echo "PASS: hello output"

set +e
"$BIN_DIR/fib"
code=$?
set -e
if [[ $code -ne 55 ]]; then
  echo "FAIL: fib exit code (want 55, got $code)"
  exit 1
fi
echo "PASS: fib exit code"

set +e
"$BIN_DIR/vec_demo"
code=$?
set -e
if [[ $code -ne 10 ]]; then
  echo "FAIL: vec_demo exit code (want 10, got $code)"
  exit 1
fi
echo "PASS: vec_demo exit code"

echo "All checks passed."
