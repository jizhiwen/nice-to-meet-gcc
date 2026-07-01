/* Minimal _start for -nostdlib (x86_64) */
	.global _start
	.section .text
_start:
	xor %rbp, %rbp
	call main
	mov %rax, %rdi
	mov $60, %rax
	syscall
	hlt
