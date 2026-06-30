# Instruction and ELF subset

## tinycc emitted instruction subset

- data move/addressing: `movq`, `movzbq`, `leaq`
- stack/frame: `pushq`, `popq`, `leave`, `ret`
- arithmetic: `addq`, `subq`, `imulq`, `negq`
- comparison/branch: `cmpq`, `sete`, `setne`, `setl`, `setle`, `setg`, `setge`, `je`, `jne`, `jmp`
- calls: `call`
- runtime-specific: `syscall`, `hlt`

## tinyas supported directives

- sections: `.text`, `.data`, `.bss`, `.section`
- symbols/metadata: `.globl`, `.type`, `.size`
- data: `.byte`, `.long`, `.quad`, `.asciz`

## tinyld output behavior

- links objects using `runtime/linker.ld`
- sets entry to `_start` by default
- outputs static ELF executable suitable for Linux `x86_64`
- optional map file via `--map`
