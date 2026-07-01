#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include <stddef.h>

/* ── ELF types ───────────────────────────────────────────────────────── */
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

/* ── ELF Header ──────────────────────────────────────────────────────── */
#define EI_NIDENT   16
#define ELFMAG      "\177ELF"
#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define ET_EXEC     2
#define ET_DYN      3
#define EM_X86_64   62

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf64_Half  e_type;
    Elf64_Half  e_machine;
    Elf64_Word  e_version;
    Elf64_Addr  e_entry;
    Elf64_Off   e_phoff;
    Elf64_Off   e_shoff;
    Elf64_Word  e_flags;
    Elf64_Half  e_ehsize;
    Elf64_Half  e_phentsize;
    Elf64_Half  e_phnum;
    Elf64_Half  e_shentsize;
    Elf64_Half  e_shnum;
    Elf64_Half  e_shstrndx;
} Elf64_Ehdr;

/* ── Program Header ──────────────────────────────────────────────────── */
#define PT_NULL     0
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PT_NOTE     4
#define PT_PHDR     6
#define PT_TLS      7

#define PF_X        0x1   /* execute */
#define PF_W        0x2   /* write */
#define PF_R        0x4   /* read */

typedef struct {
    Elf64_Word  p_type;
    Elf64_Word  p_flags;
    Elf64_Off   p_offset;
    Elf64_Addr  p_vaddr;
    Elf64_Addr  p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} Elf64_Phdr;

/* ── Dynamic section ─────────────────────────────────────────────────── */
#define DT_NULL     0
#define DT_NEEDED   1
#define DT_PLTRELSZ 2
#define DT_PLTGOT   3
#define DT_HASH     4
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_RELA     7
#define DT_RELASZ   8
#define DT_RELAENT  9
#define DT_STRSZ    10
#define DT_SYMENT   11
#define DT_INIT     12
#define DT_FINI     13
#define DT_SONAME   14
#define DT_RPATH    15
#define DT_JMPREL   23
#define DT_PLTREL   20
#define DT_DEBUG    21
#define DT_TEXTREL  22
#define DT_FLAGS    30

typedef struct {
    Elf64_Sxword d_tag;
    union { Elf64_Xword d_val; Elf64_Addr d_ptr; } d_un;
} Elf64_Dyn;

/* ── Relocation ──────────────────────────────────────────────────────── */
typedef struct {
    Elf64_Addr  r_offset;
    Elf64_Xword r_info;
    Elf64_Sxword r_addend;
} Elf64_Rela;

#define ELF64_R_SYM(i)   ((i) >> 32)
#define ELF64_R_TYPE(i)  ((i) & 0xFFFFFFFF)

#define R_X86_64_NONE        0
#define R_X86_64_64          1
#define R_X86_64_PC32        2
#define R_X86_64_GLOB_DAT    6
#define R_X86_64_JUMP_SLOT   7
#define R_X86_64_RELATIVE    8
#define R_X86_64_32          10
#define R_X86_64_32S         11

/* ── Symbol table ────────────────────────────────────────────────────── */
typedef struct {
    Elf64_Word  st_name;
    unsigned char st_info;
    unsigned char st_other;
    Elf64_Half  st_shndx;
    Elf64_Addr  st_value;
    Elf64_Xword st_size;
} Elf64_Sym;

#define ELF64_ST_BIND(i)    ((i) >> 4)
#define ELF64_ST_TYPE(i)    ((i) & 0xF)
#define STB_GLOBAL  1
#define STB_WEAK    2
#define STT_FUNC    2
#define STT_OBJECT  1
#define SHN_UNDEF   0

/* ── Auxiliary vector ────────────────────────────────────────────────── */
#define AT_NULL     0
#define AT_PHDR     3
#define AT_PHENT    4
#define AT_PHNUM    5
#define AT_PAGESZ   6
#define AT_BASE     7
#define AT_FLAGS    8
#define AT_ENTRY    9
#define AT_UID      11
#define AT_EUID     12
#define AT_GID      13
#define AT_EGID     14
#define AT_SECURE   23
#define AT_RANDOM   25

typedef struct {
    uint64_t a_type;
    uint64_t a_val;
} auxv_t;

/* ── ELF loader result ───────────────────────────────────────────────── */
typedef struct {
    uint64_t    entry;          /* entry point (may be dynlink entry) */
    uint64_t    interp_base;    /* base where interpreter was loaded */
    uint64_t    phdr_vaddr;     /* virtual addr of program headers */
    uint64_t    phdr_num;       /* number of program headers */
    uint64_t    phdr_ent;       /* sizeof program header entry */
    uint64_t    exec_entry;     /* original executable entry */
    char        interp[256];    /* interpreter path */
    int         has_interp;     /* 1 if PT_INTERP found */
} elf_load_result_t;

/*
 * Load an ELF binary from memory into the given address space.
 * base_offset: added to all virtual addresses (for PIE/shared libs, 0 for exec).
 */
int elf_load(void *data, size_t size, void *aspace,
             uint64_t base_offset, elf_load_result_t *result);

/*
 * Set up initial user stack with argc/argv/envp/auxv.
 * Returns the initial RSP for the new process.
 */
uint64_t elf_setup_stack(void *aspace, uint64_t stack_top,
                         int argc, const char **argv,
                         const char **envp, auxv_t *auxv, int nauxv);

/* Validate ELF header */
int elf_valid(void *data, size_t size);

#endif /* ELF_H */
