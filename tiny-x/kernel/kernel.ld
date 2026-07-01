/*
 * kernel.ld - Kernel linker script
 *
 * Kernel is loaded at physical 1MB (0x100000) by GRUB.
 * Uses identity mapping: virtual address == physical address.
 *
 * Layout:
 *   .multiboot2  : Multiboot2 header (must be first, in first 32KB)
 *   .text        : Executable code
 *   .rodata      : Read-only data
 *   .data        : Initialized data
 *   .bss         : Uninitialized data (zero-filled)
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x100000;           /* Load address: 1MB physical */

    kernel_start = .;

    /* Multiboot2 header must be within first 32KB */
    .multiboot2 : {
        KEEP(*(.multiboot2))
    }

    .text ALIGN(4096) : {
        *(.text.boot)       /* Boot stubs first */
        *(.text*)
    }

    .rodata ALIGN(4096) : {
        *(.rodata*)
    }

    .data ALIGN(4096) : {
        *(.data*)
    }

    /* initramfs embedded as a symbol range */
    .initramfs ALIGN(4096) : {
        initramfs_start = .;
        KEEP(*(.initramfs))
        initramfs_end = .;
    }

    .bss ALIGN(4096) : {
        bss_start = .;
        *(COMMON)
        *(.bss*)
        bss_end = .;
    }

    kernel_end = .;

    /* Discard debug info in release (keep for debugging) */
    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note.*)
    }
}
