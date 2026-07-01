/*
 * pmm.c - Physical Memory Manager
 *
 * Uses a bitmap: each bit = one 4KB page.
 * The bitmap itself lives in a statically allocated region.
 *
 * Memory up to 128MB is managed (can be extended).
 */

#include "pmm.h"
#include <string.h>

/* Support up to 512MB = 131072 pages */
#define MAX_PAGES   (512 * 1024 * 1024 / 4096)  /* 131072 */
#define BITMAP_SIZE (MAX_PAGES / 8)              /* 16384 bytes */

static uint8_t bitmap[BITMAP_SIZE];
static uint64_t total_pages  = 0;
static uint64_t used_pages   = 0;

/* ── Bitmap helpers ───────────────────────────────────────────────────── */
static void bitmap_set(uint64_t page)
{
    if (page < MAX_PAGES)
        bitmap[page / 8] |= (1 << (page % 8));
}

static void bitmap_clear(uint64_t page)
{
    if (page < MAX_PAGES)
        bitmap[page / 8] &= ~(1 << (page % 8));
}

static int bitmap_test(uint64_t page)
{
    if (page >= MAX_PAGES) return 1; /* treat out-of-range as used */
    return (bitmap[page / 8] >> (page % 8)) & 1;
}

/* ── Multiboot2 tag structures ────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t size;
} mb2_tag_hdr_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    /* followed by mmap entries */
} mb2_tag_mmap_t;

#define MB2_TAG_MMAP    6

void pmm_init(void *mb2_info, uint64_t kernel_start, uint64_t kernel_end)
{
    /* Start with everything used */
    memset(bitmap, 0xFF, sizeof(bitmap));
    total_pages = 0;
    used_pages  = 0;

    /* Parse Multiboot2 info structure */
    /* First 8 bytes: total_size, reserved */
    uint8_t *p = (uint8_t *)mb2_info + 8;
    uint32_t total_size = *(uint32_t *)mb2_info;
    uint8_t *end = (uint8_t *)mb2_info + total_size;

    while (p < end) {
        mb2_tag_hdr_t *tag = (mb2_tag_hdr_t *)p;
        if (tag->type == 0) break;  /* end tag */

        if (tag->type == MB2_TAG_MMAP) {
            mb2_tag_mmap_t *mmap_tag = (mb2_tag_mmap_t *)tag;
            uint8_t *entry_p = (uint8_t *)mmap_tag + sizeof(mb2_tag_mmap_t);
            uint8_t *mmap_end = p + tag->size;

            while (entry_p < mmap_end) {
                mb2_mmap_entry_t *entry = (mb2_mmap_entry_t *)entry_p;

                if (entry->type == MB2_MMAP_AVAILABLE) {
                    uint64_t base   = entry->base;
                    uint64_t length = entry->length;

                    /* Page-align the region */
                    uint64_t page_start = (base + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
                    uint64_t page_end   = (base + length) / PMM_PAGE_SIZE;

                    for (uint64_t pg = page_start; pg < page_end && pg < MAX_PAGES; pg++) {
                        bitmap_clear(pg);
                        total_pages++;
                    }
                }
                entry_p += mmap_tag->entry_size;
            }
        }

        /* Next tag (8-byte aligned) */
        p += (tag->size + 7) & ~7U;
    }

    /* Mark page 0 as used (NULL pointer guard) */
    bitmap_set(0);

    /* Mark kernel pages as used */
    uint64_t ks = kernel_start / PMM_PAGE_SIZE;
    uint64_t ke = (kernel_end + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    for (uint64_t pg = ks; pg < ke; pg++) {
        if (!bitmap_test(pg)) used_pages++;
        bitmap_set(pg);
    }

    /* Mark the bitmap itself as used */
    uint64_t bm_start = (uint64_t)bitmap / PMM_PAGE_SIZE;
    uint64_t bm_end   = ((uint64_t)bitmap + sizeof(bitmap) + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    for (uint64_t pg = bm_start; pg < bm_end; pg++) {
        if (!bitmap_test(pg)) used_pages++;
        bitmap_set(pg);
    }
}

uint64_t pmm_alloc(void)
{
    for (uint64_t i = 1; i < MAX_PAGES; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_pages++;
            return i * PMM_PAGE_SIZE;
        }
    }
    return 0;  /* out of memory */
}

uint64_t pmm_alloc_n(size_t n)
{
    uint64_t start = 0;
    size_t   count = 0;

    for (uint64_t i = 1; i < MAX_PAGES; i++) {
        if (!bitmap_test(i)) {
            if (count == 0) start = i;
            count++;
            if (count == n) {
                /* Mark all n pages used */
                for (uint64_t j = start; j < start + n; j++) {
                    bitmap_set(j);
                    used_pages++;
                }
                return start * PMM_PAGE_SIZE;
            }
        } else {
            count = 0;
            start = 0;
        }
    }
    return 0;
}

void pmm_free(uint64_t phys)
{
    uint64_t page = phys / PMM_PAGE_SIZE;
    if (page == 0 || page >= MAX_PAGES) return;
    if (bitmap_test(page)) {
        bitmap_clear(page);
        if (used_pages > 0) used_pages--;
    }
}

uint64_t pmm_total(void)
{
    return total_pages * PMM_PAGE_SIZE;
}

uint64_t pmm_free_bytes(void)
{
    return (total_pages - used_pages) * PMM_PAGE_SIZE;
}
