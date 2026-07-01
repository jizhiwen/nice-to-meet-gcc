/*
 * main.c - Kernel entry point (C)
 *
 * Called from boot.S after long mode is established.
 * Initializes all subsystems and starts the first process (shell).
 *
 * Multiboot2 info pointer is passed in RDI.
 */

#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/serial.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/syscall.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "proc/process.h"
#include "proc/elf.h"
#include "fs/vfs.h"
#include "fs/initramfs.h"
#include "syscall/syscalls.h"
#include <stdint.h>
#include <string.h>

/* Defined in linker script */
extern uint8_t kernel_start[];
extern uint8_t kernel_end[];
extern uint8_t initramfs_start[];
extern uint8_t initramfs_end[];

/* GS-based CPU local data setup */
static void setup_gs_base(void)
{
    extern cpu_local_t cpu_local;
    extern void tss_set_rsp0(uint64_t);

    /* Point GS base to cpu_local */
    uint64_t base = (uint64_t)&cpu_local;
    /* Write GS base via MSR */
    uint32_t lo = (uint32_t)(base & 0xFFFFFFFF);
    uint32_t hi = (uint32_t)(base >> 32);
    __asm__ volatile(
        "mov $0xC0000101, %%ecx\n"  /* MSR_GS_BASE */
        "wrmsr\n"
        : : "a"(lo), "d"(hi) : "ecx"
    );
    /* Also set KERNEL_GS_BASE (for swapgs) */
    __asm__ volatile(
        "mov $0xC0000102, %%ecx\n"  /* MSR_KERNEL_GS_BASE */
        "wrmsr\n"
        : : "a"(lo), "d"(hi) : "ecx"
    );

    /* Initialize cpu_local */
    cpu_local.self       = (uint64_t)&cpu_local;
    cpu_local.kernel_rsp = 0;  /* set per-process */
    cpu_local.user_rsp   = 0;
    cpu_local.current    = 0;
}

/* Print banner */
static void print_banner(void)
{
    vga_set_color(VGA_ATTR(VGA_LCYAN, VGA_BLACK));
    vga_puts("  _____ _             __  __\n");
    vga_puts(" |_   _(_)_ __  _   _\\ \\ / /\n");
    vga_puts("   | | | | '_ \\| | | |\\ V / \n");
    vga_puts("   | | | | | | | |_| | | |  \n");
    vga_puts("   |_| |_|_| |_|\\__, | |_|  \n");
    vga_puts("                 |___/        \n");
    vga_set_color(VGA_ATTR(VGA_LGRAY, VGA_BLACK));
    vga_puts("  tiny-x OS - x86_64 learning kernel\n\n");
}

void kernel_main(uint64_t mb2_info)
{
    /* Initialize serial first (debug output) */
    serial_init();
    serial_puts("[kernel] booting tiny-x...\n");

    /* Initialize VGA */
    vga_init();
    print_banner();

    vga_printf("[init] Multiboot2 info @ 0x%x\n", mb2_info);

    /* GDT must come first so TSS is loaded */
    vga_puts("[init] GDT... ");
    gdt_init();
    vga_puts("OK\n");

    /* IDT and PIC remapping */
    vga_puts("[init] IDT... ");
    idt_init();
    vga_puts("OK\n");

    /* Physical memory manager */
    vga_puts("[init] PMM... ");
    pmm_init((void *)mb2_info, (uint64_t)kernel_start, (uint64_t)kernel_end);
    vga_printf("OK  (free: %llu MB)\n",
               (unsigned long long)(pmm_free_bytes() / (1024 * 1024)));

    /* Virtual memory manager */
    vga_puts("[init] VMM... ");
    vmm_init();
    paging_init();
    vga_puts("OK\n");

    /* GS base for CPU-local data (needed by SYSCALL handler) */
    setup_gs_base();

    /* SYSCALL instruction setup */
    vga_puts("[init] SYSCALL... ");
    syscall_init();
    vga_puts("OK\n");

    /* Keyboard */
    vga_puts("[init] Keyboard... ");
    keyboard_init();
    vga_puts("OK\n");

    /* VFS */
    vga_puts("[init] VFS... ");
    vfs_init();
    vga_puts("OK\n");

    /* Load initramfs */
    size_t initramfs_size = (size_t)(initramfs_end - initramfs_start);
    if (initramfs_size > 0) {
        vga_printf("[init] initramfs @ 0x%x  size=%llu bytes\n",
                   (uint64_t)initramfs_start,
                   (unsigned long long)initramfs_size);
        initramfs_load(initramfs_start, initramfs_size);
        vga_puts("[init] initramfs loaded\n");
    } else {
        vga_puts("[init] no embedded initramfs\n");
        /* Try Multiboot2 module */
        /* (would parse MB2 modules tag here) */
    }

    /* Process subsystem */
    vga_puts("[init] Processes... ");
    proc_init();
    sched_init();
    vga_puts("OK\n");

    /* Enable interrupts */
    vga_puts("[init] Enabling interrupts\n");
    __asm__ volatile("sti");

    vga_puts("\n[kernel] Starting /bin/sh\n\n");
    serial_puts("[kernel] Starting /bin/sh\n");

    /* Create process 1 (init/shell) */
    process_t *init = proc_new("sh");
    if (!init) {
        vga_puts("[PANIC] failed to create init process\n");
        for (;;) __asm__("hlt");
    }
    init->aspace = vmm_new_aspace();

    /* Set current process */
    extern cpu_local_t cpu_local;
    cpu_local.current    = init;
    cpu_local.kernel_rsp = init->kstack_top;
    current = init;
    tss_set_rsp0(init->kstack_top);

    /* Execute shell */
    const char *sh_argv[] = { "/bin/sh", 0 };
    const char *sh_envp[] = { "PATH=/bin", "HOME=/", "TERM=vt100", 0 };
    int ret = proc_exec("/bin/sh", sh_argv, sh_envp);
    if (ret < 0) {
        vga_puts("[PANIC] failed to exec /bin/sh\n");
        /* Drop into kernel shell fallback */
        vga_puts("\ntiny-x kernel> ");
        for (;;) {
            int c = keyboard_getchar();
            if (c > 0) vga_putchar((char)c);
        }
    }

    /* Should never reach here */
    for (;;) __asm__("hlt");
}
