#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>

/*
 * Virtual Memory Manager (VMM)
 *
 * Manages per-process virtual address spaces using the paging layer.
 * Tracks Virtual Memory Areas (VMAs) – contiguous ranges of virtual pages
 * with uniform protection flags.
 */

/* VMA protection flags */
#define VMA_READ    (1 << 0)
#define VMA_WRITE   (1 << 1)
#define VMA_EXEC    (1 << 2)
#define VMA_SHARED  (1 << 3)
#define VMA_ANON    (1 << 4)    /* anonymous (not file-backed) */

/* Maximum VMAs per process */
#define VMM_MAX_VMAS    64

typedef struct vma {
    uint64_t    start;      /* inclusive, page-aligned */
    uint64_t    end;        /* exclusive, page-aligned */
    uint32_t    flags;      /* VMA_* */
    struct vma *next;
} vma_t;

typedef struct {
    uint64_t  *pml4;        /* physical address of PML4 */
    vma_t      vmas[VMM_MAX_VMAS];
    int        nvmas;
    uint64_t   brk;         /* current heap top */
    uint64_t   brk_base;    /* initial heap base */
} vmm_aspace_t;

/* Initialise a new address space */
vmm_aspace_t *vmm_new_aspace(void);

/* Free an address space and all its mappings */
void vmm_free_aspace(vmm_aspace_t *as);

/* Map pages [virt, virt+len) with given flags.  Allocates physical pages. */
int vmm_map(vmm_aspace_t *as, uint64_t virt, size_t len, uint32_t flags);

/* Map a specific physical region (e.g., MMIO) */
int vmm_map_phys(vmm_aspace_t *as, uint64_t virt, uint64_t phys,
                 size_t len, uint32_t flags);

/* Unmap [virt, virt+len) */
void vmm_unmap(vmm_aspace_t *as, uint64_t virt, size_t len);

/* Set brk (heap top).  Returns new brk. */
uint64_t vmm_brk(vmm_aspace_t *as, uint64_t new_brk);

/* Copy VMAs and mappings from src → dst (for fork) */
int vmm_clone(vmm_aspace_t *dst, vmm_aspace_t *src);

/* Convert VMA flags to PTE flags */
uint64_t vma_flags_to_pte(uint32_t vma_flags);

/* Kernel vmm_alloc: allocate kernel virtual+physical pages */
void *vmm_kernel_alloc(size_t bytes);
void  vmm_kernel_free(void *ptr, size_t bytes);

void vmm_init(void);

#endif /* VMM_H */
