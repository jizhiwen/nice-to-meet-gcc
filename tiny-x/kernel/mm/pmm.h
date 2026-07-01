#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

/*
 * Physical Memory Manager (PMM)
 *
 * Manages physical pages using a bitmap.
 * Each bit represents one 4KB page.
 * 0 = free, 1 = used.
 *
 * Initialized from the Multiboot2 memory map.
 */

#define PMM_PAGE_SIZE   4096UL

/* Multiboot2 mmap entry types */
#define MB2_MMAP_AVAILABLE      1
#define MB2_MMAP_RESERVED       2
#define MB2_MMAP_ACPI_RECLAIM   3
#define MB2_MMAP_NVS            4
#define MB2_MMAP_BADRAM         5

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed)) mb2_mmap_entry_t;

/* Initialise PMM from Multiboot2 info */
void pmm_init(void *mb2_info, uint64_t kernel_start, uint64_t kernel_end);

/* Allocate one physical page. Returns physical address, 0 on failure. */
uint64_t pmm_alloc(void);

/* Allocate n contiguous physical pages. Returns base physical address. */
uint64_t pmm_alloc_n(size_t n);

/* Free a physical page */
void pmm_free(uint64_t phys);

/* Return total / free memory in bytes */
uint64_t pmm_total(void);
uint64_t pmm_free_bytes(void);

#endif /* PMM_H */
