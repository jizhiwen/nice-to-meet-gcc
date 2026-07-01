.set MULTIBOOT2_MAGIC,  0xE85250D6
.set MULTIBOOT2_ARCH,   0
.set MULTIBOOT2_LEN,    (multiboot_end - multiboot_start)
.set MULTIBOOT2_CHECK,  -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH + MULTIBOOT2_LEN)

.set CR0_PG,  0x80000000
.set CR4_PAE, 0x00000020
.set EFER_LME, 0x00000100

.set XEN_ELFNOTE_PHYS32_ENTRY, 0x12

.section .note.Xen, "a"
.align 4
	.long 4
	.long 4
	.long XEN_ELFNOTE_PHYS32_ENTRY
	.asciz "Xen"
.align 4
	.long _start

.section .multiboot, "a"
.align 8
multiboot_start:
	.long MULTIBOOT2_MAGIC
	.long MULTIBOOT2_ARCH
	.long MULTIBOOT2_LEN
	.long MULTIBOOT2_CHECK
	.word 0
	.word 0
	.long 8
multiboot_end:

.section .text
.code32
.global _start
_start:
	cld
	movl %eax, %esi
	movl %ebx, %edi
	movl $stack_top, %esp
	call setup_paging
	call enter_long_mode

.code64
long_mode:
	movq $0, %rbp
	movq $stack_top, %rsp
	movq %rsi, %rdi
	movq %rdi, %rsi
	call kernel_main
1:	hlt
	jmp 1b

.code32
setup_paging:
	movl $p4_table, %edi
	movl $p3_table, %eax
	orl $3, %eax
	movl %eax, (%edi)
	movl $p3_table, %edi
	movl $0, %ecx
1:
	movl $p2_table, %eax
	movl %ecx, %edx
	shll $12, %edx
	orl %edx, %eax
	orl $3, %eax
	movl %eax, (%edi)
	addl $8, %edi
	incl %ecx
	cmpl $512, %ecx
	jb 1b
	movl $p2_table, %edi
	movl $0, %ecx
2:
	movl $0x83, %eax
	movl %ecx, %edx
	shll $21, %edx
	orl %edx, %eax
	movl %eax, (%edi)
	addl $8, %edi
	incl %ecx
	cmpl $512, %ecx
	jb 2b
	ret

enter_long_mode:
	movl $p4_table, %eax
	movl %eax, %cr3
	movl %cr4, %eax
	orl $CR4_PAE, %eax
	movl %eax, %cr4
	movl $0xC0000080, %ecx
	rdmsr
	orl $EFER_LME, %eax
	wrmsr
	movl %cr0, %eax
	orl $CR0_PG, %eax
	movl %eax, %cr0
	lgdt gdt64_ptr
	ljmp $0x08, $long_mode

.section .bss
.align 16
stack:
	.skip 4096 * 4
stack_top:

.align 4096
p4_table:
	.skip 4096
p3_table:
	.skip 4096
p2_table:
	.skip 4096

.section .rodata
.align 8
gdt64:
	.quad 0
	.quad 0x00AF9A000000FFFF
	.quad 0x00AF92000000FFFF
gdt64_ptr:
	.word gdt64_end - gdt64 - 1
	.long gdt64
gdt64_end:
