.text
.globl _start
_start:
  xorq %rbp, %rbp
  movq (%rsp), %rdi
  leaq 8(%rsp), %rsi
  call main
  movq %rax, %rdi
  movq $60, %rax
  syscall
  hlt
