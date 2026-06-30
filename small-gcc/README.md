# small-gcc

`small-gcc` is a tiny educational toolchain that mirrors the shape of `gcc + as + ld + runtime + libc + libc++` in a compact form.

It is intentionally narrow:

- target: Linux `x86_64`
- source: tiny C-like / tiny C++-style subset
- output: static ELF executable
- linker model: fixed layout, no dynamic loader

## Components

- `cmd/tinycc.py`: tiny compiler frontend, emits AT&T assembly
- `cmd/tinyas.py`: tiny assembler that encodes a constrained x86_64 subset
- `cmd/tinyld.py`: tiny linker that resolves relocations and writes ELF directly
- `runtime/crt/crt0.s`: process entry `_start`
- `runtime/libgcc_tiny.c`: compiler helper routines
- `libc_tiny/`: syscall-facing minimal libc
- `libcxx_tiny/`: tiny C++-flavored layer on top of libc_tiny

## Quick start

```bash
cd small-gcc
make all
make check
```

## Pipeline

```text
tiny source -> tinycc -> .s -> tinyas -> .o -> tinyld + runtime/libs -> ELF executable
```

## Notes

- This project is designed for learning, not production correctness.
- `tinyas` and `tinyld` are implemented in this project and do not shell out to system assemblers/linkers.
