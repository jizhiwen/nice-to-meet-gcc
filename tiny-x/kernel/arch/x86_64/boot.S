/*
 * boot.S - Kernel entry point
 *
 * GRUB (Multiboot2) enters here in 32-bit protected mode.
 * We:
 *   1. Verify Multiboot2 magic
 *   2. Build minimal 4-level page tables (identity map first 8MB)
 *   3. Enable PAE + Long Mode + Paging
 *   4. Load 64-bit GDT and far-jump to 64-bit code
 *   5. Call kernel_main(multiboot_info_ptr)
 *
 * NOTE: GAS is invoked via GCC so C preprocessor #define works.
 */

.intel_syntax noprefix

/* ── Page table entry flags ────────────────────────────────────────────── */
#define PF_P    0x01    /* Present */
#define PF_W    0x02    /* Writable */
#define PF_PS   0x80    /* 2MB Page Size (in PD entry) */

/* ── GDT selectors ─────────────────────────────────────────────────────── */
#define SEL_KCODE64  0x08
#define SEL_KDATA64  0x10

/* ══════════════════════════════════════════════════════════════════════════
 * Multiboot2 header (must appear within first 32KB, 8-byte aligned)
 * Magic=0xE85250D6, arch=0 (i386), length, checksum
 * ════════════════════════════════════════════════════════════════════════ */
.section .multiboot2, "a"
.align 8
mb2_header_start:
    .long  0xE85250D6          /* magic */
    .long  0                   /* arch: i386/protected mode */
    .long  (mb2_header_end - mb2_header_start)   /* header length */
    /* checksum: magic+arch+length+checksum must equal 0 mod 2^32 */
    .long  (0x100000000 - (0xE85250D6 + 0 + (mb2_header_end - mb2_header_start)))
    /* End tag: type=0, flags=0, size=8 */
    .short 0
    .short 0
    .long  8
mb2_header_end:

/* ══════════════════════════════════════════════════════════════════════════
 * BSS: bootstrap page tables (4KB each, 4KB aligned) + stack
 * ════════════════════════════════════════════════════════════════════════ */
.section .bss
.align 4096
.global __boot_pml4
__boot_pml4:   .space 4096

.global __boot_pdpt
__boot_pdpt:   .space 4096

.global __boot_pd
__boot_pd:     .space 4096

/* 32KB boot stack */
.space 32768
.global __boot_stack_top
__boot_stack_top:

/* ══════════════════════════════════════════════════════════════════════════
 * Data: saved multiboot pointer, 64-bit GDT
 * ════════════════════════════════════════════════════════════════════════ */
.section .data
.align 8
.global multiboot_info_ptr
multiboot_info_ptr:  .quad 0

/* ── 64-bit GDT ──────────────────────────────────────────────────────── */
.align 16
.global gdt64_table
gdt64_table:
    .quad 0x0000000000000000   /* 0x00 null */
    .quad 0x00AF9A000000FFFF   /* 0x08 kernel code: L=1, P=1, DPL=0 */
    .quad 0x00CF92000000FFFF   /* 0x10 kernel data */
    .quad 0x00AFFA000000FFFF   /* 0x18 user code:   L=1, P=1, DPL=3 */
    .quad 0x00CFF2000000FFFF   /* 0x20 user data */
    .quad 0x0000000000000000   /* 0x28 TSS low  (filled by gdt.c) */
    .quad 0x0000000000000000   /* 0x30 TSS high */
gdt64_table_end:

.global gdt64_ptr
gdt64_ptr:
    .short (gdt64_table_end - gdt64_table - 1)
    .long  gdt64_table          /* 32-bit physical address */

/* ══════════════════════════════════════════════════════════════════════════
 * 32-bit entry point  (GRUB hands control here in protected mode)
 * ════════════════════════════════════════════════════════════════════════ */
.section .text
.code32
.global _start
_start:
    cli
    cld

    /* Verify Multiboot2 reply magic (EAX must equal 0x36d76289) */
    cmp eax, 0x36d76289
    jne .halt32

    /* Save Multiboot2 info pointer */
    mov [multiboot_info_ptr],     ebx
    mov dword ptr [multiboot_info_ptr + 4], 0

    /* ── Set up 4-level page tables for long mode ──────────────────────
     * Map virtual 0-8MB → physical 0-8MB (identity map, 2MB huge pages)
     * PML4[0] → PDPT → PD[0..3] = 2MB pages at 0, 2M, 4M, 6M
     */

    /* PML4[0] = &pdpt | P|W */
    lea eax, [__boot_pdpt]
    or  eax, (PF_P | PF_W)
    mov dword ptr [__boot_pml4 + 0],  eax
    mov dword ptr [__boot_pml4 + 4],  0

    /* PDPT[0] = &pd | P|W */
    lea eax, [__boot_pd]
    or  eax, (PF_P | PF_W)
    mov dword ptr [__boot_pdpt + 0],  eax
    mov dword ptr [__boot_pdpt + 4],  0

    /* PD[0] = phys 0x000000 | P|W|PS */
    mov dword ptr [__boot_pd +  0],  (PF_P | PF_W | PF_PS | 0x000000)
    mov dword ptr [__boot_pd +  4],  0
    /* PD[1] = phys 0x200000 | P|W|PS */
    mov dword ptr [__boot_pd +  8],  (PF_P | PF_W | PF_PS | 0x200000)
    mov dword ptr [__boot_pd + 12],  0
    /* PD[2] = phys 0x400000 | P|W|PS */
    mov dword ptr [__boot_pd + 16],  (PF_P | PF_W | PF_PS | 0x400000)
    mov dword ptr [__boot_pd + 20],  0
    /* PD[3] = phys 0x600000 | P|W|PS */
    mov dword ptr [__boot_pd + 24],  (PF_P | PF_W | PF_PS | 0x600000)
    mov dword ptr [__boot_pd + 28],  0

    /* ── Enable PAE ──────────────────────────────────────────────────── */
    mov eax, cr4
    or  eax, (1 << 5)         /* CR4.PAE */
    mov cr4, eax

    /* ── Load CR3 ────────────────────────────────────────────────────── */
    lea eax, [__boot_pml4]
    mov cr3, eax

    /* ── Enable Long Mode (EFER.LME) ─────────────────────────────────── */
    mov ecx, 0xC0000080       /* IA32_EFER */
    rdmsr
    or  eax, (1 << 8)         /* LME */
    wrmsr

    /* ── Enable Paging (CR0.PG) → enters compatibility mode ─────────── */
    mov eax, cr0
    or  eax, (1 << 31) | 1   /* PG | PE */
    mov cr0, eax

    /* ── Load 64-bit GDT ─────────────────────────────────────────────── */
    lgdt [gdt64_ptr]

    /* ── Far jump to 64-bit code segment ────────────────────────────── */
    /* Encode: jmp far SEL_KCODE64:long_mode_entry */
    .byte 0xEA                /* far jmp opcode (32-bit form) */
    .long long_mode_entry     /* 32-bit target offset */
    .word SEL_KCODE64         /* segment selector */

.halt32:
    hlt
    jmp .halt32

/* ══════════════════════════════════════════════════════════════════════════
 * 64-bit entry
 * ════════════════════════════════════════════════════════════════════════ */
.code64
long_mode_entry:
    /* Reload data segments */
    mov ax, SEL_KDATA64
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    /* Set up 64-bit kernel stack */
    lea rsp, [rip + __boot_stack_top]

    /* Pass multiboot info ptr as first argument (RDI) */
    mov edi, dword ptr [rip + multiboot_info_ptr]

    /* Clear RFLAGS */
    push 0x200          /* IF=1 (interrupts enabled after STI) */
    popfq

    /* Call C kernel */
    call kernel_main

.halt64:
    hlt
    jmp .halt64
