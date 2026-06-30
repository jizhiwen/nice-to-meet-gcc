# small-gcc architecture

`small-gcc` models the major pieces of a native toolchain with constrained behavior.

## Stage flow

1. `tinycc` parses tiny source and emits x86_64 AT&T assembly.
2. `tinyas` validates the restricted assembly subset and invokes GNU `as`.
3. `tinyld` links one user object plus runtime/lib objects with a fixed script.
4. Linux executes `_start` from `crt0`, then `main`, then exits via syscall.

## Minimal ABI assumptions

- SysV x86_64 integer calling convention (first 6 args in registers)
- 64-bit signed integer computation for user expressions
- return value in `%rax`
- stack frame with `%rbp` base and 16-byte aligned allocation

## Runtime stack

- `crt0.s` provides process entry and `main` bridge
- `libgcc_tiny.c` provides division/modulo helper symbols
- `libc_tiny.c` provides raw syscall wrappers + helper APIs
- `libcxx_tiny.cpp` provides a tiny C++-style vector abstraction and demo function

## ELF/link model

- static executable only
- fixed section order: `.text`, `.rodata`, `.data`, `.bss`
- no dynamic linker or shared libraries
