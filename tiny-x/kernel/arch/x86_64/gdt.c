/*
 * gdt.c - Global Descriptor Table
 *
 * x86_64 uses a flat memory model; segments are mostly vestigial.
 * We still need:
 *   - Separate code/data descriptors for user vs kernel (ring switch)
 *   - A TSS (Task State Segment) for the kernel stack pointer on interrupts
 *
 * The initial GDT entries are in boot.S (gdt64_table).
 * Here we install the TSS descriptor at runtime.
 */

#include "gdt.h"
#include <stdint.h>
#include <string.h>

/* TSS for this CPU */
tss64_t kernel_tss __attribute__((aligned(16)));

/* The GDT and pointer are defined in boot.S */
extern uint64_t gdt64_table[];

/*
 * Write a 64-bit TSS descriptor into slots [5] and [6] of the GDT.
 * A 64-bit system descriptor occupies two 8-byte slots.
 *
 *  Bits [15:0]  = limit[15:0]
 *  Bits [31:16] = base[15:0]
 *  Bits [39:32] = base[23:16]
 *  Bits [43:40] = type (0x9 = 64-bit available TSS)
 *  Bit  [44]    = 0 (system descriptor)
 *  Bits [46:45] = DPL = 0
 *  Bit  [47]    = present = 1
 *  Bits [51:48] = limit[19:16]
 *  Bits [55:52] = flags (G=0, 64-bit, AVL=0)
 *  Bits [63:56] = base[31:24]
 *  Bits [95:64] = base[63:32]   ← second 8-byte slot
 *  Bits [127:96] = reserved (must be zero)
 */
static void install_tss(void)
{
    uint64_t base  = (uint64_t)&kernel_tss;
    uint32_t limit = sizeof(kernel_tss) - 1;

    uint64_t low = 0, high = 0;

    low |= (uint64_t)(limit & 0xFFFF);           /* limit[15:0] */
    low |= (uint64_t)(base  & 0xFFFF)     << 16; /* base[15:0]  */
    low |= (uint64_t)((base >> 16) & 0xFF)<< 32; /* base[23:16] */
    low |= (uint64_t)0x89                 << 40; /* P=1, DPL=0, type=0x9 */
    low |= (uint64_t)((limit >> 16) & 0xF)<< 48;/* limit[19:16]*/
    low |= (uint64_t)((base >> 24) & 0xFF)<< 56; /* base[31:24] */

    high = (base >> 32) & 0xFFFFFFFF;            /* base[63:32] */

    gdt64_table[5] = low;
    gdt64_table[6] = high;
}

void gdt_init(void)
{
    memset(&kernel_tss, 0, sizeof(kernel_tss));
    kernel_tss.iopb_offset = sizeof(kernel_tss);  /* no I/O permission bitmap */

    install_tss();

    /* Load TR (Task Register) with TSS selector */
    __asm__ volatile("ltr %0" : : "r"((uint16_t)SEG_TSS));
}

void tss_set_rsp0(uint64_t rsp0)
{
    kernel_tss.rsp0 = rsp0;
}
