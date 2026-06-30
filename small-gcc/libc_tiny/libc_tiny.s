.text
.globl tiny_write
tiny_write:
  movq $1, %rax
  syscall
  ret

.globl tiny_exit
tiny_exit:
  movq $60, %rax
  syscall
  hlt

.globl tiny_put_newline
tiny_put_newline:
  movq $1, %rdi
  leaq .L_nl(%rip), %rsi
  movq $1, %rdx
  call tiny_write
  ret

.section .rodata
.L_nl:
  .asciz "\n"
