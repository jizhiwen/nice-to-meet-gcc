#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* Number of IDT entries (256 vectors) */
#define IDT_ENTRIES     256

/* x86_64 interrupt frame pushed by hardware + our stub */
typedef struct __attribute__((packed)) {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;            /* interrupt vector number */
    uint64_t error_code;        /* pushed by CPU for some exceptions */
    uint64_t rip, cs, rflags, rsp, ss;
} interrupt_frame_t;

typedef void (*isr_handler_t)(interrupt_frame_t *frame);

void idt_init(void);
void idt_set_handler(uint8_t vector, isr_handler_t handler);

/* IRQ base: IRQ0 = vector 32 */
#define IRQ_BASE        32
#define IRQ_KEYBOARD    (IRQ_BASE + 1)
#define IRQ_TIMER       (IRQ_BASE + 0)

/* 8259 PIC I/O ports */
#define PIC1_CMD        0x20
#define PIC1_DATA       0x21
#define PIC2_CMD        0xA0
#define PIC2_DATA       0xA1
#define PIC_EOI         0x20

static inline void pic_eoi(uint8_t irq)
{
    if (irq >= 8) {
        __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PIC_EOI), "Nd"((uint16_t)PIC2_CMD));
    }
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)PIC_EOI), "Nd"((uint16_t)PIC1_CMD));
}

#endif /* IDT_H */
