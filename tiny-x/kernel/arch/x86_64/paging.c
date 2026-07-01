/*
 * paging.c - x86_64 4-level page table management
 *
 * We use 4KB pages everywhere (no huge pages in user space).
 * The kernel identity-maps itself during boot; we copy those
 * entries into every new address space so kernel code is always
 * accessible after a SYSCALL.
 *
 * Hierarchy: PML4 → PDPT → PD → PT → 4KB page
 */

#include "paging.h"
#include "../mm/pmm.h"
#include <string.h>

/* Boot PML4 from boot.S – kernel entries are in here */
extern pte_t __boot_pml4[512];

/* ── Allocate a zeroed page table ────────────────────────────────────── */
pte_t *paging_alloc_table(void)
{
    uint64_t phys = pmm_alloc();
    if (!phys) return 0;
    pte_t *tbl = (pte_t *)phys;
    memset(tbl, 0, PAGE_SIZE);
    return tbl;
}

/*
 * paging_new_aspace - create a fresh PML4 with kernel mappings copied
 *
 * Upper half of PML4 (indices 256-511) is kernel space.
 * We share those entries with the boot PML4 so the kernel
 * remains mapped after switching address spaces.
 */
pte_t *paging_new_aspace(void)
{
    pte_t *pml4 = paging_alloc_table();
    if (!pml4) return 0;

    /* Copy kernel half of boot PML4 (entries 256-511) */
    for (int i = 256; i < 512; i++) {
        pml4[i] = __boot_pml4[i];
    }
    /* Also copy entry 0 (our identity map covers kernel at 1MB) */
    pml4[0] = __boot_pml4[0];

    return pml4;
}

/*
 * paging_get_or_alloc - walk/create a page table at a given level
 * Returns pointer to the next-level table, allocating if needed.
 */
static pte_t *get_or_alloc(pte_t *parent, int idx, uint64_t flags)
{
    if (!(parent[idx] & PTE_PRESENT)) {
        pte_t *child = paging_alloc_table();
        if (!child) return 0;
        parent[idx] = (uint64_t)child | flags | PTE_PRESENT;
    }
    return (pte_t *)(parent[idx] & PTE_ADDR_MASK);
}

void paging_map(pte_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags)
{
    pte_t *pdpt = get_or_alloc(pml4, PML4_IDX(virt),
                               PTE_PRESENT | PTE_WRITE | (flags & PTE_USER));
    if (!pdpt) return;

    pte_t *pd = get_or_alloc(pdpt, PDPT_IDX(virt),
                             PTE_PRESENT | PTE_WRITE | (flags & PTE_USER));
    if (!pd) return;

    pte_t *pt = get_or_alloc(pd, PD_IDX(virt),
                             PTE_PRESENT | PTE_WRITE | (flags & PTE_USER));
    if (!pt) return;

    pt[PT_IDX(virt)] = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;
    paging_invlpg(virt);
}

void paging_unmap(pte_t *pml4, uint64_t virt)
{
    if (!(pml4[PML4_IDX(virt)] & PTE_PRESENT)) return;
    pte_t *pdpt = (pte_t *)(pml4[PML4_IDX(virt)] & PTE_ADDR_MASK);

    if (!(pdpt[PDPT_IDX(virt)] & PTE_PRESENT)) return;
    pte_t *pd = (pte_t *)(pdpt[PDPT_IDX(virt)] & PTE_ADDR_MASK);

    if (!(pd[PD_IDX(virt)] & PTE_PRESENT)) return;
    pte_t *pt = (pte_t *)(pd[PD_IDX(virt)] & PTE_ADDR_MASK);

    pt[PT_IDX(virt)] = 0;
    paging_invlpg(virt);
}

uint64_t paging_virt_to_phys(pte_t *pml4, uint64_t virt)
{
    if (!(pml4[PML4_IDX(virt)] & PTE_PRESENT)) return 0;
    pte_t *pdpt = (pte_t *)(pml4[PML4_IDX(virt)] & PTE_ADDR_MASK);

    if (!(pdpt[PDPT_IDX(virt)] & PTE_PRESENT)) return 0;
    pte_t *pd = (pte_t *)(pdpt[PDPT_IDX(virt)] & PTE_ADDR_MASK);

    if (!(pd[PD_IDX(virt)] & PTE_PRESENT)) return 0;

    /* Check for 2MB huge page */
    if (pd[PD_IDX(virt)] & PTE_HUGE) {
        return (pd[PD_IDX(virt)] & PTE_ADDR_MASK) | (virt & 0x1FFFFF);
    }

    pte_t *pt = (pte_t *)(pd[PD_IDX(virt)] & PTE_ADDR_MASK);
    if (!(pt[PT_IDX(virt)] & PTE_PRESENT)) return 0;

    return (pt[PT_IDX(virt)] & PTE_ADDR_MASK) | (virt & 0xFFF);
}

void paging_init(void)
{
    /* Boot page tables are already active; nothing to do except
     * record the boot PML4 as the kernel reference. */
}
