/*
 * vmm.c - Virtual Memory Manager
 */

#include "vmm.h"
#include "pmm.h"
#include "../arch/x86_64/paging.h"
#include <string.h>

/* Pool of address space structs (simple static allocation) */
#define MAX_ASPACES 32
static vmm_aspace_t aspace_pool[MAX_ASPACES];
static int          aspace_used[MAX_ASPACES];

void vmm_init(void)
{
    memset(aspace_pool, 0, sizeof(aspace_pool));
    memset(aspace_used, 0, sizeof(aspace_used));
}

vmm_aspace_t *vmm_new_aspace(void)
{
    for (int i = 0; i < MAX_ASPACES; i++) {
        if (!aspace_used[i]) {
            aspace_used[i] = 1;
            vmm_aspace_t *as = &aspace_pool[i];
            memset(as, 0, sizeof(*as));
            as->pml4 = paging_new_aspace();
            return as;
        }
    }
    return 0;
}

void vmm_free_aspace(vmm_aspace_t *as)
{
    /* Unmap and free all user-space pages */
    for (int i = 0; i < as->nvmas; i++) {
        vma_t *v = &as->vmas[i];
        for (uint64_t va = v->start; va < v->end; va += PAGE_SIZE) {
            uint64_t phys = paging_virt_to_phys(as->pml4, va);
            if (phys) {
                paging_unmap(as->pml4, va);
                pmm_free(phys);
            }
        }
    }
    /* Free the PML4 itself */
    pmm_free((uint64_t)as->pml4);

    /* Return slot */
    int idx = (int)(as - aspace_pool);
    if (idx >= 0 && idx < MAX_ASPACES)
        aspace_used[idx] = 0;
}

uint64_t vma_flags_to_pte(uint32_t flags)
{
    uint64_t pte = PTE_USER;
    if (flags & VMA_WRITE) pte |= PTE_WRITE;
    if (!(flags & VMA_EXEC)) pte |= PTE_NX;
    return pte;
}

static vma_t *vma_add(vmm_aspace_t *as, uint64_t start, uint64_t end, uint32_t flags)
{
    if (as->nvmas >= VMM_MAX_VMAS) return 0;
    vma_t *v = &as->vmas[as->nvmas++];
    v->start = start;
    v->end   = end;
    v->flags = flags;
    v->next  = 0;
    return v;
}

int vmm_map(vmm_aspace_t *as, uint64_t virt, size_t len, uint32_t flags)
{
    uint64_t start = virt & PAGE_MASK;
    uint64_t end   = PAGE_ALIGN(virt + len);
    uint64_t pte_flags = vma_flags_to_pte(flags);

    for (uint64_t va = start; va < end; va += PAGE_SIZE) {
        uint64_t phys = pmm_alloc();
        if (!phys) return -1;
        memset((void *)phys, 0, PAGE_SIZE);
        paging_map(as->pml4, va, phys, pte_flags);
    }
    vma_add(as, start, end, flags);
    return 0;
}

int vmm_map_phys(vmm_aspace_t *as, uint64_t virt, uint64_t phys,
                 size_t len, uint32_t flags)
{
    uint64_t start     = virt & PAGE_MASK;
    uint64_t end       = PAGE_ALIGN(virt + len);
    uint64_t phys_base = phys & PAGE_MASK;
    uint64_t pte_flags = vma_flags_to_pte(flags);

    for (uint64_t off = 0; off < (end - start); off += PAGE_SIZE) {
        paging_map(as->pml4, start + off, phys_base + off, pte_flags);
    }
    vma_add(as, start, end, flags);
    return 0;
}

void vmm_unmap(vmm_aspace_t *as, uint64_t virt, size_t len)
{
    uint64_t start = virt & PAGE_MASK;
    uint64_t end   = PAGE_ALIGN(virt + len);

    for (uint64_t va = start; va < end; va += PAGE_SIZE) {
        uint64_t phys = paging_virt_to_phys(as->pml4, va);
        if (phys) {
            paging_unmap(as->pml4, va);
            pmm_free(phys);
        }
    }
    /* Remove VMAs (simplified: just shrink/remove) */
    for (int i = 0; i < as->nvmas; i++) {
        vma_t *v = &as->vmas[i];
        if (v->start >= start && v->end <= end) {
            /* Remove this VMA by shifting */
            for (int j = i; j < as->nvmas - 1; j++)
                as->vmas[j] = as->vmas[j+1];
            as->nvmas--;
            i--;
        }
    }
}

uint64_t vmm_brk(vmm_aspace_t *as, uint64_t new_brk)
{
    if (new_brk == 0) return as->brk;

    if (new_brk > as->brk) {
        /* Expand heap */
        uint64_t old_end = PAGE_ALIGN(as->brk);
        uint64_t new_end = PAGE_ALIGN(new_brk);
        if (new_end > old_end) {
            vmm_map(as, old_end, new_end - old_end, VMA_READ | VMA_WRITE | VMA_ANON);
        }
    } else if (new_brk < as->brk) {
        /* Shrink heap */
        uint64_t new_end = PAGE_ALIGN(new_brk);
        uint64_t old_end = PAGE_ALIGN(as->brk);
        if (new_end < old_end) {
            vmm_unmap(as, new_end, old_end - new_end);
        }
    }
    as->brk = new_brk;
    return new_brk;
}

int vmm_clone(vmm_aspace_t *dst, vmm_aspace_t *src)
{
    for (int i = 0; i < src->nvmas; i++) {
        vma_t *v = &src->vmas[i];
        /* Allocate fresh physical pages and copy content */
        for (uint64_t va = v->start; va < v->end; va += PAGE_SIZE) {
            uint64_t src_phys = paging_virt_to_phys(src->pml4, va);
            uint64_t dst_phys = pmm_alloc();
            if (!dst_phys) return -1;
            if (src_phys)
                memcpy((void *)dst_phys, (void *)src_phys, PAGE_SIZE);
            else
                memset((void *)dst_phys, 0, PAGE_SIZE);
            paging_map(dst->pml4, va, dst_phys, vma_flags_to_pte(v->flags));
        }
        vma_add(dst, v->start, v->end, v->flags);
    }
    dst->brk      = src->brk;
    dst->brk_base = src->brk_base;
    return 0;
}

/* ── Kernel virtual memory allocation ───────────────────────────────── */
/* Simple bump allocator above kernel_end */
extern uint64_t kernel_end;
static uint64_t kvirt_bump = 0;

void *vmm_kernel_alloc(size_t bytes)
{
    if (!kvirt_bump) kvirt_bump = PAGE_ALIGN((uint64_t)&kernel_end);
    size_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    void *ptr = (void *)kvirt_bump;
    for (size_t i = 0; i < pages; i++) {
        uint64_t phys = pmm_alloc();
        if (!phys) return 0;
        /* In our identity-mapped kernel, virtual == physical.
           Map in the boot PML4 which is current. */
        extern pte_t __boot_pml4[512];
        paging_map(__boot_pml4, kvirt_bump + i * PAGE_SIZE,
                   phys, PTE_WRITE | PTE_PRESENT);
    }
    kvirt_bump += pages * PAGE_SIZE;
    return ptr;
}

void vmm_kernel_free(void *ptr, size_t bytes)
{
    /* Not implemented: kernel heap never shrinks in this minimal implementation */
    (void)ptr; (void)bytes;
}
