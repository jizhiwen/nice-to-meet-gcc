/*
 * idt.c - Interrupt Descriptor Table
 *
 * Sets up 256 IDT entries.  Each entry points to an ASM stub
 * (in irq.S) that saves registers, calls a C handler, then restores.
 *
 * After IDT, reprograms the 8259 PIC to move IRQ0-15 to vectors 32-47
 * (avoids conflict with CPU exception vectors 0-31).
 */

#include "idt.h"
#include <stdint.h>
#include <string.h>

/* ── IDT entry (gate descriptor) ─────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint16_t offset_low;    /* handler[15:0]  */
    uint16_t selector;      /* code segment (SEG_KCODE = 0x08) */
    uint8_t  ist;           /* IST index (0 = no stack switch) */
    uint8_t  type_attr;     /* type + DPL + present */
    uint16_t offset_mid;    /* handler[31:16] */
    uint32_t offset_high;   /* handler[63:32] */
    uint32_t reserved;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idt_ptr_t;

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idt_ptr;
static isr_handler_t handlers[IDT_ENTRIES];

/* Defined in irq.S: array of 256 stub addresses */
extern void *isr_stub_table[IDT_ENTRIES];

static void idt_set_entry(uint8_t n, void *handler, uint8_t type_attr)
{
    uint64_t addr = (uint64_t)handler;
    idt[n].offset_low  = addr & 0xFFFF;
    idt[n].selector    = 0x08;           /* kernel code segment */
    idt[n].ist         = 0;
    idt[n].type_attr   = type_attr;
    idt[n].offset_mid  = (addr >> 16) & 0xFFFF;
    idt[n].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[n].reserved    = 0;
}

/* ── 8259 PIC remapping ───────────────────────────────────────────────── */
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void pic_remap(void)
{
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1_CMD,  0x11);  /* ICW1: init + ICW4 needed */
    outb(PIC2_CMD,  0x11);
    outb(PIC1_DATA, IRQ_BASE);      /* ICW2: PIC1 starts at vector 32 */
    outb(PIC2_DATA, IRQ_BASE + 8);  /* ICW2: PIC2 starts at vector 40 */
    outb(PIC1_DATA, 0x04);  /* ICW3: PIC1 has slave at IRQ2 */
    outb(PIC2_DATA, 0x02);  /* ICW3: PIC2 is slave on line 2 */
    outb(PIC1_DATA, 0x01);  /* ICW4: 8086 mode */
    outb(PIC2_DATA, 0x01);

    /* Restore saved masks */
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

/* ── Master C dispatcher ─────────────────────────────────────────────── */
void isr_dispatch(interrupt_frame_t *frame)
{
    uint64_t vec = frame->vector;

    if (handlers[vec]) {
        handlers[vec](frame);
    } else if (vec < 32) {
        /* Unhandled CPU exception – print and halt */
        extern void vga_puts(const char *);
        extern void vga_puthex(uint64_t);
        vga_puts("\n[EXCEPTION] vector=0x");
        vga_puthex(vec);
        vga_puts(" err=0x");
        vga_puthex(frame->error_code);
        vga_puts(" rip=0x");
        vga_puthex(frame->rip);
        vga_puts("\n");
        for (;;) __asm__ volatile("hlt");
    }

    /* Send EOI to PIC for IRQs */
    if (vec >= (uint64_t)IRQ_BASE && vec < (uint64_t)(IRQ_BASE + 16)) {
        pic_eoi((uint8_t)(vec - IRQ_BASE));
    }
}

void idt_set_handler(uint8_t vector, isr_handler_t handler)
{
    handlers[vector] = handler;
}

void idt_init(void)
{
    memset(idt, 0, sizeof(idt));
    memset(handlers, 0, sizeof(handlers));

    pic_remap();

    /* Install all 256 stubs */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        /* 0x8E = present | DPL=0 | 64-bit interrupt gate */
        idt_set_entry((uint8_t)i, isr_stub_table[i], 0x8E);
    }

    /* Load IDT */
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint64_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
}
