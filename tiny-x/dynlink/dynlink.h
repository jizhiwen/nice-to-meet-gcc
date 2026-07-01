#ifndef DYNLINK_H
#define DYNLINK_H

#include <stdint.h>
#include <stddef.h>

/*
 * dynlink.h - Dynamic linker public interface
 *
 * The dynamic linker (ld-tiny.so) is responsible for:
 *   1. Parsing the main executable's dynamic section
 *   2. Loading all DT_NEEDED shared libraries
 *   3. Performing relocations (GLOB_DAT, JUMP_SLOT, RELATIVE, etc.)
 *   4. Calling DT_INIT functions
 *   5. Jumping to the main executable's entry point
 *
 * Entry: _dl_start (called by kernel via PT_INTERP mechanism)
 */

/* Maximum number of shared objects loaded at once */
#define DYNLINK_MAX_LIBS    32

/* Shared object descriptor */
typedef struct so_info {
    char        name[256];      /* soname */
    char        path[256];      /* full path loaded from */
    uint64_t    base;           /* load base address */
    uint64_t    entry;          /* entry point (if executable) */

    /* Dynamic section entries */
    uint64_t    strtab;         /* DT_STRTAB */
    uint64_t    symtab;         /* DT_SYMTAB */
    uint64_t    rela;           /* DT_RELA */
    size_t      relasz;         /* DT_RELASZ */
    uint64_t    jmprel;         /* DT_JMPREL */
    size_t      jmprelsz;       /* DT_PLTRELSZ */
    uint64_t    pltgot;         /* DT_PLTGOT */
    uint64_t    init;           /* DT_INIT */
    uint64_t    fini;           /* DT_FINI */
    size_t      strsz;          /* DT_STRSZ */
    size_t      syment;         /* DT_SYMENT */

    /* Parsed structures */
    void       *phdr;           /* program headers in memory */
    int         phnum;

    struct so_info *next;
} so_info_t;

/* Symbol lookup result */
typedef struct {
    uint64_t    value;          /* resolved virtual address */
    so_info_t  *lib;            /* which library provides it */
} sym_result_t;

/* Public API (called by PLT stubs via GOT[2]) */
uint64_t _dl_runtime_resolve(so_info_t *link_map, uint64_t reloc_offset);

/* Entry point called by kernel */
void __attribute__((noreturn)) _dl_start(uint64_t *sp);

#endif /* DYNLINK_H */
