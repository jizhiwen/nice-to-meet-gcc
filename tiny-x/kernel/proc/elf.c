/*
 * elf.c - ELF64 loader
 *
 * Loads ELF executables and shared libraries into a process address space.
 * Handles:
 *   - PT_LOAD segments
 *   - PT_INTERP (dynamic linker path)
 *   - Stack setup with argc/argv/envp/auxv
 */

#include "elf.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../arch/x86_64/paging.h"
#include <string.h>
#include <stddef.h>

int elf_valid(void *data, size_t size)
{
    if (size < sizeof(Elf64_Ehdr)) return 0;
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)data;
    if (memcmp(ehdr->e_ident, ELFMAG, 4) != 0) return 0;
    if (ehdr->e_ident[4] != ELFCLASS64) return 0;
    if (ehdr->e_ident[5] != ELFDATA2LSB) return 0;
    if (ehdr->e_machine != EM_X86_64) return 0;
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) return 0;
    return 1;
}

int elf_load(void *data, size_t size, void *aspace_ptr,
             uint64_t base_offset, elf_load_result_t *result)
{
    vmm_aspace_t *as = (vmm_aspace_t *)aspace_ptr;
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)data;

    if (!elf_valid(data, size)) return -1;

    memset(result, 0, sizeof(*result));
    result->exec_entry = ehdr->e_entry + base_offset;

    /* Scan program headers */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr *phdr = (Elf64_Phdr *)((uint8_t *)data + ehdr->e_phoff
                                           + i * ehdr->e_phentsize);
        switch (phdr->p_type) {
        case PT_LOAD: {
            if (phdr->p_memsz == 0) break;

            uint64_t vaddr = phdr->p_vaddr + base_offset;
            uint64_t vend  = vaddr + phdr->p_memsz;
            uint64_t valign_start = vaddr & PAGE_MASK;
            uint64_t valign_end   = PAGE_ALIGN(vend);
            size_t   map_len      = (size_t)(valign_end - valign_start);

            /* Build VMA flags */
            uint32_t vma_flags = 0;
            if (phdr->p_flags & PF_R) vma_flags |= VMA_READ;
            if (phdr->p_flags & PF_W) vma_flags |= VMA_WRITE;
            if (phdr->p_flags & PF_X) vma_flags |= VMA_EXEC;

            /* Map pages (zero-filled) */
            vmm_map(as, valign_start, map_len, vma_flags | VMA_WRITE);

            /* Copy file data */
            if (phdr->p_filesz > 0) {
                uint8_t *src = (uint8_t *)data + phdr->p_offset;
                uint8_t *dst = (uint8_t *)vaddr;
                /* In our identity-mapped kernel, virtual == physical.
                   We need to walk the page table to get physical pages
                   and copy via their physical addresses. */
                size_t filesz = phdr->p_filesz;
                uint64_t copy_vaddr = vaddr;
                size_t copied = 0;
                while (copied < filesz) {
                    uint64_t pa = paging_virt_to_phys(as->pml4, copy_vaddr);
                    if (!pa) break;
                    size_t page_off = copy_vaddr & (PAGE_SIZE - 1);
                    size_t chunk = PAGE_SIZE - page_off;
                    if (chunk > filesz - copied) chunk = filesz - copied;
                    memcpy((void *)(pa + page_off), src + copied, chunk);
                    copied += chunk;
                    copy_vaddr += chunk;
                }
                (void)dst; /* suppress warning */
            }
            break;
        }
        case PT_INTERP: {
            if (phdr->p_filesz < sizeof(result->interp)) {
                memcpy(result->interp,
                       (uint8_t *)data + phdr->p_offset,
                       phdr->p_filesz);
                result->interp[phdr->p_filesz] = '\0';
                result->has_interp = 1;
            }
            break;
        }
        case PT_PHDR: {
            result->phdr_vaddr = phdr->p_vaddr + base_offset;
            break;
        }
        default:
            break;
        }
    }

    result->phdr_num = ehdr->e_phnum;
    result->phdr_ent = ehdr->e_phentsize;
    if (!result->phdr_vaddr)
        result->phdr_vaddr = base_offset + ehdr->e_phoff;
    result->entry = result->exec_entry;

    return 0;
}

/*
 * elf_setup_stack - Build initial process stack
 *
 * Stack layout (top → bottom, addresses decrease):
 *   [padding to 16-byte align]
 *   auxv[] (AT_NULL terminated)
 *   envp[] (NULL terminated)
 *   argv[] (NULL terminated)
 *   argc
 *
 * RSP points to &argc on return.
 */
uint64_t elf_setup_stack(void *aspace_ptr, uint64_t stack_top,
                         int argc, const char **argv,
                         const char **envp, auxv_t *auxv, int nauxv)
{
    vmm_aspace_t *as = (vmm_aspace_t *)aspace_ptr;

    /* Allocate 8MB user stack */
    uint64_t stack_size = 8 * 1024 * 1024UL;
    uint64_t stack_base = stack_top - stack_size;
    vmm_map(as, stack_base, stack_size, VMA_READ | VMA_WRITE | VMA_ANON);

    /*
     * We build the stack in physical memory via page table walk.
     * Helper to write a uint64_t at a given virtual address:
     */
    uint64_t sp = stack_top;

    /* Helper: write 8 bytes at virtual address va in address space */
    #define WRITE64(va, val) do { \
        uint64_t _pa = paging_virt_to_phys(as->pml4, (va) & PAGE_MASK); \
        if (_pa) { \
            *(uint64_t *)(_pa + ((va) & (PAGE_SIZE-1))) = (uint64_t)(val); \
        } \
    } while(0)

    /* Copy strings onto stack first */
    /* We put strings just above where we'll put the argv/envp arrays */
    /* Simple approach: copy all strings to the stack area */

    /* Count envp */
    int envc = 0;
    if (envp) while (envp[envc]) envc++;

    /* Calculate total string space needed */
    size_t str_space = 0;
    for (int i = 0; i < argc; i++) str_space += (argv[i] ? __builtin_strlen(argv[i]) + 1 : 1);
    for (int i = 0; i < envc; i++) str_space += (envp[i] ? __builtin_strlen(envp[i]) + 1 : 1);
    /* Add random bytes for AT_RANDOM */
    str_space += 16;
    str_space = (str_space + 15) & ~15UL;  /* align */

    sp -= str_space;
    uint64_t str_area = sp;

    /* Copy strings */
    uint64_t arg_ptrs[256], env_ptrs[256];
    uint64_t cur = str_area;

    for (int i = 0; i < argc && i < 255; i++) {
        arg_ptrs[i] = cur;
        const char *s = argv[i] ? argv[i] : "";
        size_t len = __builtin_strlen(s) + 1;
        /* Copy byte by byte via physical pages */
        for (size_t j = 0; j < len; j++) {
            uint64_t pa = paging_virt_to_phys(as->pml4, cur & PAGE_MASK);
            if (pa) ((char *)(pa + (cur & (PAGE_SIZE-1))))[0] = s[j];
            cur++;
        }
    }
    for (int i = 0; i < envc && i < 255; i++) {
        env_ptrs[i] = cur;
        const char *s = envp[i] ? envp[i] : "";
        size_t len = __builtin_strlen(s) + 1;
        for (size_t j = 0; j < len; j++) {
            uint64_t pa = paging_virt_to_phys(as->pml4, cur & PAGE_MASK);
            if (pa) ((char *)(pa + (cur & (PAGE_SIZE-1))))[0] = s[j];
            cur++;
        }
    }
    uint64_t random_ptr = cur;
    /* 16 random bytes (use simple pseudo-random) */
    for (int i = 0; i < 16; i++) {
        uint64_t pa = paging_virt_to_phys(as->pml4, cur & PAGE_MASK);
        if (pa) ((uint8_t *)(pa + (cur & (PAGE_SIZE-1))))[0] = (uint8_t)(i * 37 + 0x5A);
        cur++;
    }

    /* Align sp to 8 bytes */
    sp &= ~7UL;

    /* auxv (push in reverse) */
    sp -= 2 * 8;  WRITE64(sp, 0);     WRITE64(sp+8, 0);          /* AT_NULL */
    for (int i = nauxv - 1; i >= 0; i--) {
        sp -= 2 * 8;
        WRITE64(sp,   auxv[i].a_type);
        WRITE64(sp+8, auxv[i].a_val);
    }
    /* Patch AT_RANDOM */
    sp -= 2 * 8;  WRITE64(sp, AT_RANDOM);  WRITE64(sp+8, random_ptr);

    /* envp[] (NULL terminated) */
    sp -= 8;  WRITE64(sp, 0);  /* NULL */
    for (int i = envc - 1; i >= 0; i--) {
        sp -= 8;  WRITE64(sp, env_ptrs[i]);
    }

    /* argv[] (NULL terminated) */
    sp -= 8;  WRITE64(sp, 0);  /* NULL */
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 8;  WRITE64(sp, arg_ptrs[i]);
    }

    /* argc */
    sp -= 8;  WRITE64(sp, (uint64_t)argc);

    /* Align to 16 bytes (ABI requirement) */
    sp &= ~15UL;

    #undef WRITE64
    return sp;
}
