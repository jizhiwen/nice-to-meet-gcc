#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* GDT segment selectors */
#define SEG_NULL        0x00
#define SEG_KCODE       0x08    /* kernel code  (DPL=0) */
#define SEG_KDATA       0x10    /* kernel data  (DPL=0) */
#define SEG_UCODE       0x18    /* user code    (DPL=3) */
#define SEG_UDATA       0x20    /* user data    (DPL=3) */
#define SEG_TSS         0x28    /* TSS (16-byte descriptor) */

/* RPL bits for user-space ring-3 selectors */
#define RPL3            0x03
#define SEG_UCODE_RPL3  (SEG_UCODE | RPL3)
#define SEG_UDATA_RPL3  (SEG_UDATA | RPL3)

/* 64-bit Task State Segment */
typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;          /* kernel stack for ring-0 entries */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];        /* interrupt stack table */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} tss64_t;

extern tss64_t kernel_tss;

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);

#endif /* GDT_H */
