#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>

/* ── Page sizes ──────────────────────────────────────────────────────── */
#define PAGE_SIZE       4096UL
#define PAGE_SHIFT      12
#define PAGE_MASK       (~(PAGE_SIZE - 1))
#define PAGE_ALIGN(x)   (((x) + PAGE_SIZE - 1) & PAGE_MASK)

/* ── Page entry flags ────────────────────────────────────────────────── */
#define PTE_PRESENT     (1UL << 0)
#define PTE_WRITE       (1UL << 1)
#define PTE_USER        (1UL << 2)
#define PTE_PWT         (1UL << 3)
#define PTE_PCD         (1UL << 4)
#define PTE_ACCESSED    (1UL << 5)
#define PTE_DIRTY       (1UL << 6)
#define PTE_HUGE        (1UL << 7)   /* PDE: 2MB page */
#define PTE_GLOBAL      (1UL << 8)
#define PTE_NX          (1UL << 63)  /* No-Execute */

#define PTE_ADDR_MASK   0x000FFFFFFFFFF000UL

/* Extract physical address from a PTE */
#define PTE_PHYS(pte)   ((pte) & PTE_ADDR_MASK)

/* Virtual address to page-table index macros */
#define PML4_IDX(va)    (((va) >> 39) & 0x1FF)
#define PDPT_IDX(va)    (((va) >> 30) & 0x1FF)
#define PD_IDX(va)      (((va) >> 21) & 0x1FF)
#define PT_IDX(va)      (((va) >> 12) & 0x1FF)

typedef uint64_t pte_t;

/* ── Address space ranges ────────────────────────────────────────────── */
#define KERNEL_BASE     0x100000UL          /* kernel at 1MB */
#define USER_BASE       0x400000UL          /* user code start */
#define USER_STACK_TOP  0x7FFFFFFFE000UL    /* user stack top */
#define DYNLINK_BASE    0x7F0000000000UL    /* dynamic linker */

/*
 * Each process has its own PML4.  Kernel pages are always mapped
 * in the top half of every address space for fast syscall access.
 */

/* Allocate a new page table (one page, zeroed) */
pte_t *paging_alloc_table(void);

/* Create a new address space (PML4) with kernel entries copied */
pte_t *paging_new_aspace(void);

/* Map a virtual address to a physical address in the given PML4 */
void paging_map(pte_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);

/* Unmap a virtual address */
void paging_unmap(pte_t *pml4, uint64_t virt);

/* Resolve virtual → physical in a given PML4 (returns 0 if not mapped) */
uint64_t paging_virt_to_phys(pte_t *pml4, uint64_t virt);

/* Load CR3 with a PML4 physical address */
static inline void paging_load_cr3(pte_t *pml4)
{
    __asm__ volatile("movq %0, %%cr3" : : "r"((uint64_t)pml4) : "memory");
}

/* Invalidate a single TLB entry */
static inline void paging_invlpg(uint64_t virt)
{
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

/* Get current CR3 */
static inline uint64_t paging_get_cr3(void)
{
    uint64_t cr3;
    __asm__ volatile("movq %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void paging_init(void);

#endif /* PAGING_H */
