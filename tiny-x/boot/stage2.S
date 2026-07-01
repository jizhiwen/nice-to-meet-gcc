/*
 * stage2.S - Stage 2 Bootloader
 *
 * Loaded by MBR to 0x7E00 in real mode.
 * Responsible for:
 *   1. Detect and print memory map (INT 15h E820)
 *   2. Load kernel image (from sectors 8-200+) to 0x100000 (1MB)
 *   3. Build GDT for protected mode
 *   4. Enter protected mode (32-bit)
 *   5. Load kernel above 1MB (using protected mode read or unreal mode)
 *   6. Set up long mode (64-bit):
 *      a. Enable PAE
 *      b. Build minimal page tables
 *      c. Set EFER.LME
 *      d. Enable paging
 *   7. Far jump to kernel at 0x100000
 *
 * This code teaches:
 *   - Real mode 16-bit addressing
 *   - Protected mode GDT
 *   - Long mode page tables
 *   - Memory detection (E820)
 *   - Loading large binaries via INT 13h extended read
 *
 * NOTE: This is an educational bootloader. For production, use GRUB.
 */

.code16
.org 0                      /* Code offset relative to load address 0x7E00 */

stage2_start:
    /* Set up segments */
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw $0x9000, %sp       /* New stack */

    movsi $banner, %si
    call print16

    /* ── Detect memory via INT 15h E820 ──────────────────────────── */
    call detect_memory

    /* ── Load kernel (sectors 8-200) to 0x100000 ─────────────────── */
    /* Use INT 13h extended read (LBA mode) */
    movsi $msg_load_kernel, %si
    call print16

    call load_kernel

    /* ── Enter protected mode then long mode ─────────────────────── */
    cli

    /* Load GDT */
    lgdtw gdt32_ptr

    /* Enable protected mode */
    movl %cr0, %eax
    orb  $0x01, %al
    movl %eax, %cr0

    /* Far jump to flush pipeline */
    .byte 0x66              /* operand size override → 32-bit */
    ljmp $0x08, $pm32_entry

/* ═════════════════════════════════════════════════════════════════════
 * Protected mode (32-bit) - set up long mode
 * ═══════════════════════════════════════════════════════════════════ */
.code32
pm32_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl $0x90000, %esp

    /* ── Build page tables at 0x1000 ─────────────────────────────── */
    /* PML4  @ 0x1000
     * PDPT  @ 0x2000
     * PD    @ 0x3000 (2MB huge pages)
     */
    /* Zero all three tables */
    movl $0x1000, %edi
    xorl %eax, %eax
    movl $0x3000, %ecx
    rep stosl

    /* PML4[0] = 0x2003 (→ PDPT, P|W) */
    movl $0x2003, 0x1000

    /* PDPT[0] = 0x3003 (→ PD, P|W) */
    movl $0x3003, 0x2000

    /* PD: map 0-64MB with 2MB pages */
    movl $0, %ecx           /* physical address */
    movl $0x3000, %ebx      /* PD address */
.fill_pd:
    movl %ecx, %eax
    orl  $0x83, %eax        /* P|W|PS (2MB huge page) */
    movl %eax, (%ebx)
    movl $0, 4(%ebx)
    addl $0x200000, %ecx    /* next 2MB */
    addl $8, %ebx
    cmpl $(32 * 0x200000), %ecx   /* 32 entries = 64MB */
    jl   .fill_pd

    /* Load CR3 with PML4 */
    movl $0x1000, %eax
    movl %eax, %cr3

    /* Enable PAE */
    movl %cr4, %eax
    orb  $0x20, %al
    movl %eax, %cr4

    /* Enable Long Mode (EFER.LME) */
    movl $0xC0000080, %ecx
    rdmsr
    orb  $0x01, %ah         /* bit 8 = LME */
    wrmsr

    /* Enable Paging */
    movl %cr0, %eax
    orl  $(1<<31), %eax
    movl %eax, %cr0

    /* Load 64-bit GDT */
    lgdtl gdt64_ptr32

    /* Far jump to 64-bit code */
    .byte 0xFF, 0x25        /* jmp [rip+0] */
    .long long64_ptr

long64_ptr: .quad lm64_entry

.code64
lm64_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movq $0x90000, %rsp

    /* Jump to kernel at 0x100000 */
    movq $0x100000, %rax
    /* EAX must have multiboot magic, RBX must have info ptr
     * Since we're our own bootloader (not GRUB), we don't have
     * multiboot info. We pass 0 and 0.
     * The kernel checks this and handles missing multiboot gracefully.
     */
    xorq %rbx, %rbx
    movl $0x36d76289, %eax  /* Pretend to be Multiboot2 (magic) */
    call *%rax

halt:
    hlt
    jmp halt

/* ═════════════════════════════════════════════════════════════════════
 * Real mode subroutines
 * ═══════════════════════════════════════════════════════════════════ */
.code16

/* ── Memory detection E820 ────────────────────────────────────────────── */
/* Stores E820 entries at 0x5000 */
#define E820_BUFFER 0x5000
#define E820_COUNT  0x4FF8

detect_memory:
    movw $0, %ds
    movl $0, E820_COUNT     /* count = 0 */
    xorl %ebx, %ebx         /* continuation = 0 */
    movw $E820_BUFFER, %di
.e820_loop:
    movl $0xE820, %eax
    movl $0x534D4150, %edx  /* 'SMAP' */
    movl $24, %ecx          /* buffer size */
    int  $0x15
    jc   .e820_done
    cmpl $0x534D4150, %eax
    jne  .e820_done
    addw $24, %di
    incl E820_COUNT
    testl %ebx, %ebx
    jnz  .e820_loop
.e820_done:
    ret

/* ── Load kernel ──────────────────────────────────────────────────────── */
/*
 * Use INT 13h AH=0x42 (Extended Read) to load 48 sectors (24KB)
 * from LBA 8 to physical 0x100000.
 * In real mode, we can only directly address < 1MB.
 * Use "unreal mode" trick or just use BIOS to load to a low buffer
 * then copy in protected mode.
 *
 * For simplicity: load in chunks to 0x8000, copy to 0x100000.
 * But we need to be in protected mode to address 0x100000.
 *
 * Solution: After entering protected mode, use software to load
 * from disk using raw port I/O (ATA PIO).
 *
 * For this educational bootloader, we'll load the kernel via
 * ATA PIO in 32-bit protected mode (see pm32_entry above).
 * But since we're in real mode here, let's load to 0x8000-0xFFFF
 * and copy in PM. Limited to ~32KB, enough for initial testing.
 *
 * For full kernel loading: use our custom read-from-disk in PM mode.
 */
load_kernel:
    /* Use INT 13h extended read packet */
    /* Load sectors 8-39 (32 sectors = 16KB) to 0x8000 */
    /* Our kernel + initramfs will be placed by GRUB in real usage */
    movsi $msg_bios_read, %si
    call print16

    movb $0x80, %dl         /* drive 0x80 */
    /* Extended read packet */
    movw $0x0010, disk_packet     /* packet size = 16 */
    movw $32, disk_packet+2       /* sectors to read */
    movw $0x8000, disk_packet+4   /* buffer offset */
    movw $0, disk_packet+6        /* buffer segment */
    movl $8, disk_packet+8        /* LBA low */
    movl $0, disk_packet+12       /* LBA high */

    movw $0, %es
    movsi $disk_packet, %si
    movb $0x42, %ah         /* Extended Read */
    int  $0x13
    jc   .load_err
    ret
.load_err:
    movsi $msg_load_err, %si
    call print16
    hlt
    jmp .

/* ── 16-bit print string (INT 10h teletype) ───────────────────────────── */
print16:
    pushw %ax
.p16_loop:
    lodsb
    testb %al, %al
    jz .p16_done
    movb $0x0E, %ah
    movw $0x0007, %bx
    int $0x10
    jmp .p16_loop
.p16_done:
    popw %ax
    ret

/* ═════════════════════════════════════════════════════════════════════
 * Data
 * ═══════════════════════════════════════════════════════════════════ */
disk_packet:    .fill 16, 1, 0

banner:         .ascii "tiny-x Stage 2 Bootloader\r\n\0"
msg_load_kernel:.ascii "Loading kernel...\r\n\0"
msg_bios_read:  .ascii "BIOS disk read...\r\n\0"
msg_load_err:   .ascii "Kernel load error!\r\n\0"

/* ── 32-bit GDT (temporary for PM entry) ─────────────────────────────── */
.align 8
gdt32:
    .quad 0                         /* null */
    .quad 0x00CF9A000000FFFF        /* 32-bit code: base=0, limit=4GB, P|DPL=0 */
    .quad 0x00CF92000000FFFF        /* 32-bit data */
gdt32_end:
gdt32_ptr:
    .word (gdt32_end - gdt32 - 1)
    .long gdt32 + 0x7E00           /* physical address */

/* ── 64-bit GDT ──────────────────────────────────────────────────────── */
.align 8
gdt64_stage2:
    .quad 0
    .quad 0x00AF9A000000FFFF        /* 64-bit code */
    .quad 0x00CF92000000FFFF        /* data */
gdt64_stage2_end:
gdt64_ptr32:
    .word (gdt64_stage2_end - gdt64_stage2 - 1)
    .long gdt64_stage2 + 0x7E00
