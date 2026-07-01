/*
 * mbr.S - Master Boot Record (Stage 1 Bootloader)
 *
 * The BIOS loads exactly 512 bytes from LBA 0 into memory at 0x7C00.
 * Our MBR must:
 *   1. Set up a working stack
 *   2. Enable A20 line (allow access to memory > 1MB)
 *   3. Load Stage 2 from disk (sectors 1-4) to 0x7E00
 *   4. Jump to Stage 2
 *
 * This teaches:
 *   - Real mode (16-bit x86)
 *   - BIOS INT 13h disk I/O
 *   - The A20 line problem
 *   - Memory map constraints below 1MB
 *
 * Must be exactly 512 bytes with 0x55AA signature at end.
 */

.code16
.global _start
_start:
    /* ── Set up segments ──────────────────────────────────────────── */
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp       /* Stack below MBR */
    sti

    /* ── Save boot drive (BIOS puts it in DL) ────────────────────── */
    movb %dl, boot_drive

    /* ── Print loading message ────────────────────────────────────── */
    movsi $msg_loading, %si
    call print_string

    /* ── Enable A20 via keyboard controller ──────────────────────── */
    call a20_enable

    /* ── Load Stage 2 (sectors 1-4) to 0x7E00 ────────────────────── */
    /*
     * BIOS INT 13h AH=0x02: Read Sectors
     *   AL = number of sectors to read
     *   CH = cylinder number (low 8 bits)
     *   CL = sector number (1-based) | cylinder high bits [7:6]
     *   DH = head number
     *   DL = drive number
     *   ES:BX = buffer address
     */
    movb boot_drive, %dl
    movb $0x02, %ah         /* Read sectors */
    movb $4,    %al         /* Read 4 sectors (2KB) */
    movb $0,    %ch         /* Cylinder 0 */
    movb $2,    %cl         /* Sector 2 (1-based, sector after MBR) */
    movb $0,    %dh         /* Head 0 */
    movw $0,    %bx
    movw %bx,   %es
    movw $0x7E00, %bx       /* Load to 0:0x7E00 */

    int $0x13
    jc  disk_error

    /* ── Jump to Stage 2 ──────────────────────────────────────────── */
    jmp $0, $0x7E00

disk_error:
    movsi $msg_disk_err, %si
    call print_string
    hlt
    jmp .

/* ── A20 enable (keyboard controller method) ─────────────────────────── */
a20_enable:
    /* Check if A20 is already enabled */
    movw $0xFFFF, %ax
    movw %ax, %es
    movw $0x0510, %di       /* 0xFFFF:0510 = physical 0x10050F */
    movw $0, %ax
    movw %ax, %ds
    movw $0x0500, %si       /* 0x0000:0500 = physical 0x000500 */
    movb (%si), %al
    movb %es:(%di), %bl
    movb $0x00, (%si)
    movb $0xFF, %es:(%di)
    cmpb $0xFF, (%si)
    jne  a20_already_on     /* addresses differ → A20 on */
    movb %al, (%si)
    movb %bl, %es:(%di)

    /* Enable via keyboard controller */
    call a20_wait
    movb $0xAD, %al         /* Disable keyboard */
    outb %al, $0x64
    call a20_wait
    movb $0xD0, %al         /* Read output port */
    outb %al, $0x64
    call a20_wait2
    inb  $0x60, %al
    pushw %ax
    call a20_wait
    movb $0xD1, %al         /* Write output port */
    outb %al, $0x64
    call a20_wait
    popw %ax
    orb  $0x02, %al         /* Set A20 bit */
    outb %al, $0x60
    call a20_wait
    movb $0xAE, %al         /* Enable keyboard */
    outb %al, $0x64
    call a20_wait
a20_already_on:
    xorw %ax, %ax
    movw %ax, %es
    ret

a20_wait:
    inb $0x64, %al
    testb $0x02, %al
    jnz a20_wait
    ret
a20_wait2:
    inb $0x64, %al
    testb $0x01, %al
    jz a20_wait2
    ret

/* ── Print string (BIOS INT 10h) ──────────────────────────────────────── */
print_string:
    movb $0x0E, %ah         /* BIOS teletype output */
.next_char:
    lodsb                   /* al = *si++, advance si */
    testb %al, %al
    jz .done_print
    int $0x10
    jmp .next_char
.done_print:
    ret

/* ── Data ────────────────────────────────────────────────────────────── */
boot_drive:     .byte 0x80
msg_loading:    .ascii "Loading tiny-x...\r\n\0"
msg_disk_err:   .ascii "Disk error!\r\n\0"

/* ── Pad to 512 bytes with MBR signature ──────────────────────────────── */
.org 446                    /* Partition table starts here */
/* Partition table entry 1: bootable, type 0x83 (Linux), covers whole disk */
.byte 0x80                  /* bootable */
.byte 0x00, 0x02, 0x00      /* CHS start */
.byte 0x83                  /* type: Linux */
.byte 0xFF, 0xFF, 0xFF      /* CHS end */
.long 0x00000001            /* LBA start = sector 1 */
.long 0x00080000            /* size: 512MB */
.org 462                    /* entries 2-4 empty */
.fill 48, 1, 0

.org 510
.byte 0x55, 0xAA            /* MBR signature */
