/*
 * dynlink.c - Dynamic linker (ld-tiny.so)
 *
 * This is a position-independent executable (PIE) that acts as the
 * program interpreter (PT_INTERP) for dynamically-linked programs.
 *
 * Execution flow:
 *   1. Kernel loads ld-tiny.so and the main executable
 *   2. Kernel sets up the stack (argc/argv/envp/auxv)
 *   3. Kernel jumps to _dl_start (ld-tiny.so entry)
 *   4. _dl_start reads auxv to find executable info
 *   5. Load all DT_NEEDED libraries
 *   6. Perform relocations
 *   7. Call DT_INIT functions
 *   8. Jump to executable entry point
 *
 * Relocation types handled:
 *   R_X86_64_RELATIVE   - base + addend
 *   R_X86_64_GLOB_DAT   - symbol value
 *   R_X86_64_JUMP_SLOT  - PLT slot → symbol address
 *   R_X86_64_64         - symbol + addend
 *
 * The PLT lazy resolution stub (GOT[2]) points to _dl_runtime_resolve.
 */

#include "dynlink.h"
#include <stdint.h>
#include <stddef.h>

/* ── Minimal syscall wrappers (can't use libc yet!) ──────────────────── */
static long _sys_write(int fd, const void *buf, size_t n) {
    long r;
    __asm__ volatile("syscall"
        : "=a"(r)
        : "0"(1L), "D"((long)fd), "S"(buf), "d"(n)
        : "rcx","r11","memory");
    return r;
}

static long _sys_open(const char *path, int flags, int mode) {
    long r;
    register long r8 __asm__("r8") = 0;
    register long r10 __asm__("r10") = (long)mode;
    __asm__ volatile("syscall"
        : "=a"(r)
        : "0"(2L), "D"(path), "S"((long)flags), "d"(r10), "r"(r8)
        : "rcx","r11","memory");
    return r;
}

static long _sys_close(int fd) {
    long r;
    __asm__ volatile("syscall"
        : "=a"(r) : "0"(3L), "D"((long)fd) : "rcx","r11","memory");
    return r;
}

static long _sys_read(int fd, void *buf, size_t n) {
    long r;
    __asm__ volatile("syscall"
        : "=a"(r)
        : "0"(0L), "D"((long)fd), "S"(buf), "d"(n)
        : "rcx","r11","memory");
    return r;
}

static void *_sys_mmap(void *addr, size_t len, int prot, int flags, int fd, long off) {
    long r;
    register long r8  __asm__("r8")  = (long)fd;
    register long r9  __asm__("r9")  = off;
    register long r10 __asm__("r10") = (long)flags;
    __asm__ volatile("syscall"
        : "=a"(r)
        : "0"(9L), "D"(addr), "S"(len), "d"(prot), "r"(r10), "r"(r8), "r"(r9)
        : "rcx","r11","memory");
    return (void *)r;
}

static void _sys_exit(int code) {
    __asm__ volatile("syscall"
        : : "a"(60L), "D"((long)code) : "rcx","r11","memory");
    for(;;);
}

static void _dl_puts(const char *s) {
    size_t n = 0; while(s[n]) n++;
    _sys_write(2, s, n);
}

static void _dl_panic(const char *msg) {
    _dl_puts("[dynlink] PANIC: "); _dl_puts(msg); _dl_puts("\n");
    _sys_exit(127);
}

/* ── Minimal memory ops ──────────────────────────────────────────────── */
static void _memcpy(void *dst, const void *src, size_t n) {
    char *d=dst; const char *s=src; for(size_t i=0;i<n;i++) d[i]=s[i];
}
static void _memset(void *dst, int c, size_t n) {
    char *d=dst; for(size_t i=0;i<n;i++) d[i]=(char)c;
}
static int _strcmp(const char *a, const char *b) {
    while(*a&&*a==*b){a++;b++;}
    return (unsigned char)*a-(unsigned char)*b;
}
static size_t _strlen(const char *s) { size_t n=0; while(s[n]) n++; return n; }
static void _strcpy(char *dst, const char *src) { while((*dst++=*src++)); }

/* ── ELF types (minimal) ─────────────────────────────────────────────── */
/* (Re-declare locally to avoid header dependency issues) */
typedef struct { uint64_t a_type; uint64_t a_val; } dl_auxv_t;
typedef struct { uint32_t p_type; uint32_t p_flags; uint64_t p_offset;
                 uint64_t p_vaddr; uint64_t p_paddr;
                 uint64_t p_filesz; uint64_t p_memsz; uint64_t p_align; } dl_phdr_t;
typedef struct { int32_t  d_tag; union { uint64_t d_val; uint64_t d_ptr; } d_un; } dl_dyn_t;
typedef struct { uint32_t st_name; uint8_t st_info; uint8_t st_other;
                 uint16_t st_shndx; uint64_t st_value; uint64_t st_size; } dl_sym_t;
typedef struct { uint64_t r_offset; uint64_t r_info; int64_t r_addend; } dl_rela_t;

#define DL_PT_LOAD     1
#define DL_PT_DYNAMIC  2
#define DL_DT_NEEDED   1
#define DL_DT_STRTAB   5
#define DL_DT_SYMTAB   6
#define DL_DT_RELA     7
#define DL_DT_RELASZ   8
#define DL_DT_STRSZ    10
#define DL_DT_SYMENT   11
#define DL_DT_INIT     12
#define DL_DT_FINI     13
#define DL_DT_SONAME   14
#define DL_DT_JMPREL   23
#define DL_DT_PLTRELSZ 2
#define DL_DT_PLTGOT   3
#define DL_DT_NULL     0

#define DL_R_SYM(i)    ((i) >> 32)
#define DL_R_TYPE(i)   ((uint32_t)(i))
#define DL_R_RELATIVE  8
#define DL_R_GLOB_DAT  6
#define DL_R_JUMP_SLOT 7
#define DL_R_64        1

#define DL_STB_GLOBAL  1
#define DL_STB_WEAK    2
#define DL_STN_UNDEF   0
#define DL_PF_R        4
#define DL_PF_W        2
#define DL_PF_X        1
#define DL_PROT_READ   1
#define DL_PROT_WRITE  2
#define DL_PROT_EXEC   4
#define DL_MAP_PRIVATE 2
#define DL_MAP_ANON    0x20
#define DL_MAP_FIXED   0x10

#define ELFMAG "\177ELF"

/* ── Loaded library table ────────────────────────────────────────────── */
static so_info_t _libs[DYNLINK_MAX_LIBS];
static int       _nlibs = 0;

/* ── Library search paths ────────────────────────────────────────────── */
static const char *_lib_paths[] = { "/lib", "/usr/lib", 0 };

static so_info_t *_find_lib(const char *name) {
    for (int i = 0; i < _nlibs; i++)
        if (_strcmp(_libs[i].name, name) == 0) return &_libs[i];
    return 0;
}

static so_info_t *_alloc_lib(void) {
    if (_nlibs >= DYNLINK_MAX_LIBS) _dl_panic("too many libraries");
    return &_libs[_nlibs++];
}

/* ── Load an ELF shared object from memory ───────────────────────────── */
static int _load_elf_mem(const void *data, size_t size,
                          uint64_t base_hint, so_info_t *so)
{
    const uint8_t *buf = (const uint8_t *)data;
    if (size < 64 || buf[0] != 0x7F || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F')
        return -1;

    uint64_t e_phoff    = *(uint64_t *)(buf + 32);
    uint16_t e_phnum    = *(uint16_t *)(buf + 56);
    uint16_t e_phentsize = *(uint16_t *)(buf + 54);
    uint64_t e_entry    = *(uint64_t *)(buf + 24);
    uint16_t e_type     = *(uint16_t *)(buf + 16);  /* ET_DYN=3 */

    /* Calculate load size for PIE/DSO */
    uint64_t load_min = (uint64_t)-1, load_max = 0;
    for (int i = 0; i < e_phnum; i++) {
        const uint8_t *ph = buf + e_phoff + i * e_phentsize;
        uint32_t p_type = *(uint32_t *)ph;
        if (p_type != DL_PT_LOAD) continue;
        uint64_t p_vaddr = *(uint64_t *)(ph + 16);
        uint64_t p_memsz = *(uint64_t *)(ph + 40);
        if (p_vaddr < load_min) load_min = p_vaddr;
        if (p_vaddr + p_memsz > load_max) load_max = p_vaddr + p_memsz;
    }
    if (load_min == (uint64_t)-1) return -1;

    /* For ET_DYN: choose a load base */
    uint64_t base;
    if (e_type == 3) {  /* ET_DYN */
        if (base_hint) base = base_hint;
        else {
            /* Use a heuristic base */
            static uint64_t next_base = 0x7FC000000000ULL;
            base = next_base;
            next_base += (load_max - load_min + 0x200000) & ~0x1FFFFFULL;
        }
    } else {
        base = 0;  /* ET_EXEC */
    }

    /* Map each PT_LOAD segment */
    for (int i = 0; i < e_phnum; i++) {
        const uint8_t *ph = buf + e_phoff + i * e_phentsize;
        uint32_t p_type   = *(uint32_t *)(ph + 0);
        uint32_t p_flags  = *(uint32_t *)(ph + 4);
        uint64_t p_offset = *(uint64_t *)(ph + 8);
        uint64_t p_vaddr  = *(uint64_t *)(ph + 16);
        uint64_t p_filesz = *(uint64_t *)(ph + 32);
        uint64_t p_memsz  = *(uint64_t *)(ph + 40);

        if (p_type != DL_PT_LOAD) continue;

        int prot = 0;
        if (p_flags & DL_PF_R) prot |= DL_PROT_READ;
        if (p_flags & DL_PF_W) prot |= DL_PROT_WRITE;
        if (p_flags & DL_PF_X) prot |= DL_PROT_EXEC;

        uint64_t vaddr    = base + p_vaddr;
        uint64_t vaddr_pg = vaddr & ~0xFFFULL;
        uint64_t vend_pg  = (vaddr + p_memsz + 0xFFF) & ~0xFFFULL;
        size_t   msize    = (size_t)(vend_pg - vaddr_pg);

        void *mapped = _sys_mmap((void *)vaddr_pg, msize,
                                  DL_PROT_READ | DL_PROT_WRITE,
                                  DL_MAP_PRIVATE | DL_MAP_ANON | DL_MAP_FIXED,
                                  -1, 0);
        if ((long)mapped < 0) _dl_panic("mmap failed");

        /* Copy file data */
        if (p_filesz > 0)
            _memcpy((void *)vaddr, buf + p_offset, (size_t)p_filesz);

        /* Zero BSS (p_memsz > p_filesz) */
        if (p_memsz > p_filesz)
            _memset((void *)(vaddr + p_filesz), 0, (size_t)(p_memsz - p_filesz));
    }

    so->base  = base;
    so->entry = base + e_entry;
    so->phdr  = (void *)(base + e_phoff);  /* approximate */
    so->phnum = e_phnum;

    /* Parse dynamic section */
    for (int i = 0; i < e_phnum; i++) {
        const uint8_t *ph = buf + e_phoff + i * e_phentsize;
        uint32_t p_type  = *(uint32_t *)(ph + 0);
        uint64_t p_vaddr = *(uint64_t *)(ph + 16);
        uint64_t p_memsz = *(uint64_t *)(ph + 40);
        if (p_type != DL_PT_DYNAMIC) continue;

        dl_dyn_t *dyn = (dl_dyn_t *)(base + p_vaddr);
        for (size_t j = 0; j * sizeof(dl_dyn_t) < p_memsz; j++) {
            if (dyn[j].d_tag == DL_DT_NULL) break;
            switch (dyn[j].d_tag) {
            case DL_DT_STRTAB:  so->strtab   = base + dyn[j].d_un.d_ptr; break;
            case DL_DT_SYMTAB:  so->symtab   = base + dyn[j].d_un.d_ptr; break;
            case DL_DT_RELA:    so->rela      = base + dyn[j].d_un.d_ptr; break;
            case DL_DT_RELASZ:  so->relasz    = (size_t)dyn[j].d_un.d_val; break;
            case DL_DT_JMPREL:  so->jmprel    = base + dyn[j].d_un.d_ptr; break;
            case DL_DT_PLTRELSZ:so->jmprelsz  = (size_t)dyn[j].d_un.d_val; break;
            case DL_DT_PLTGOT:  so->pltgot    = base + dyn[j].d_un.d_ptr; break;
            case DL_DT_INIT:    so->init       = base + dyn[j].d_un.d_val; break;
            case DL_DT_FINI:    so->fini       = base + dyn[j].d_un.d_val; break;
            case DL_DT_STRSZ:   so->strsz      = (size_t)dyn[j].d_un.d_val; break;
            case DL_DT_SYMENT:  so->syment     = (size_t)dyn[j].d_un.d_val; break;
            case DL_DT_SONAME: {
                /* Will resolve after strtab is known */
                break;
            }
            }
        }
        break;
    }

    return 0;
}

/* ── Load a shared library by name ──────────────────────────────────── */
static so_info_t *_load_library(const char *name)
{
    /* Check if already loaded */
    so_info_t *existing = _find_lib(name);
    if (existing) return existing;

    /* Find the file */
    char path[512];
    long fd = -1;
    for (int i = 0; _lib_paths[i]; i++) {
        _strcpy(path, _lib_paths[i]);
        /* path += "/" + name */
        size_t plen = _strlen(path);
        path[plen] = '/';
        _strcpy(path + plen + 1, name);
        fd = _sys_open(path, 0, 0);
        if (fd >= 0) break;
    }
    if (fd < 0) {
        _dl_puts("[dynlink] warning: cannot find library: ");
        _dl_puts(name); _dl_puts("\n");
        return 0;
    }

    /* Read file into memory using mmap (find size first via fstat) */
    /* We'll just read up to 4MB */
    size_t max_size = 4 * 1024 * 1024;
    void *buf = _sys_mmap(0, max_size, DL_PROT_READ | DL_PROT_WRITE,
                           DL_MAP_PRIVATE | DL_MAP_ANON, -1, 0);
    if ((long)buf < 0) { _sys_close((int)fd); _dl_panic("mmap for lib"); }

    size_t total = 0;
    while (total < max_size) {
        long r = _sys_read((int)fd, (char *)buf + total, 65536);
        if (r <= 0) break;
        total += (size_t)r;
    }
    _sys_close((int)fd);

    so_info_t *so = _alloc_lib();
    _memset(so, 0, sizeof(*so));
    _strcpy(so->name, name);
    _strcpy(so->path, path);

    if (_load_elf_mem(buf, total, 0, so) < 0) {
        _nlibs--;
        _dl_puts("[dynlink] warning: failed to load "); _dl_puts(name); _dl_puts("\n");
        return 0;
    }

    /* Don't free buf yet - the shared lib data is there */
    /* In a real impl we'd keep the mmap and use MAP_FIXED for segments */

    return so;
}

/* ── Symbol lookup ───────────────────────────────────────────────────── */
static uint64_t _lookup_symbol(const char *name, so_info_t *skip)
{
    for (int i = 0; i < _nlibs; i++) {
        so_info_t *so = &_libs[i];
        if (so == skip) continue;
        if (!so->symtab || !so->strtab) continue;

        dl_sym_t *syms = (dl_sym_t *)so->symtab;
        size_t syment  = so->syment ? so->syment : sizeof(dl_sym_t);

        /* Walk symbol table */
        for (int j = 1; ; j++) {
            dl_sym_t *sym = (dl_sym_t *)((char *)syms + j * syment);
            if (!sym->st_name && !sym->st_value && !sym->st_size) break;
            if (sym->st_shndx == DL_STN_UNDEF) continue;
            uint8_t bind = (sym->st_info >> 4);
            if (bind != DL_STB_GLOBAL && bind != DL_STB_WEAK) continue;
            const char *sym_name = (const char *)(so->strtab + sym->st_name);
            if (_strcmp(sym_name, name) == 0)
                return so->base + sym->st_value;
        }
    }
    return 0;
}

/* ── Apply relocations ───────────────────────────────────────────────── */
static void _apply_relocations(so_info_t *so, uint64_t rela_off, size_t relasz)
{
    dl_rela_t *relas = (dl_rela_t *)rela_off;
    size_t nrela = relasz / sizeof(dl_rela_t);

    for (size_t i = 0; i < nrela; i++) {
        uint64_t r_off  = so->base + relas[i].r_offset;
        uint32_t r_type = DL_R_TYPE(relas[i].r_info);
        uint32_t r_sym  = (uint32_t)DL_R_SYM(relas[i].r_info);
        int64_t  r_add  = relas[i].r_addend;

        uint64_t sym_val = 0;
        if (r_sym && so->symtab && so->strtab) {
            dl_sym_t *sym = (dl_sym_t *)(so->symtab + r_sym * (so->syment ? so->syment : sizeof(dl_sym_t)));
            const char *name = (const char *)(so->strtab + sym->st_name);
            if (sym->st_shndx == DL_STN_UNDEF) {
                sym_val = _lookup_symbol(name, 0);
            } else {
                sym_val = so->base + sym->st_value;
            }
        }

        uint64_t *target = (uint64_t *)r_off;
        switch (r_type) {
        case DL_R_RELATIVE:
            *target = so->base + (uint64_t)r_add;
            break;
        case DL_R_GLOB_DAT:
            *target = sym_val;
            break;
        case DL_R_JUMP_SLOT:
            *target = sym_val;
            break;
        case DL_R_64:
            *target = sym_val + (uint64_t)r_add;
            break;
        default:
            break;
        }
    }
}

/* ── Set up PLT GOT[0,1,2] ───────────────────────────────────────────── */
static void _setup_plt(so_info_t *so)
{
    if (!so->pltgot) return;
    uint64_t *got = (uint64_t *)so->pltgot;
    /* GOT[0] = &_DYNAMIC (already set by linker)
     * GOT[1] = link_map ptr (us)
     * GOT[2] = _dl_runtime_resolve
     */
    got[1] = (uint64_t)so;
    got[2] = (uint64_t)&_dl_runtime_resolve;
}

/* ── Runtime PLT resolver ────────────────────────────────────────────── */
/*
 * Called by PLT stub when a symbol is first accessed:
 *   push  reloc_index
 *   push  GOT[1]  (link_map)
 *   jmp   GOT[2]  (_dl_runtime_resolve)
 *
 * The stub layout means rdi=link_map, rsi=reloc_offset when called
 * from our PLT[0] stub.  We find the symbol, patch the GOT, return.
 */
uint64_t _dl_runtime_resolve(so_info_t *so, uint64_t reloc_offset)
{
    dl_rela_t *rela = (dl_rela_t *)(so->jmprel + reloc_offset);
    uint32_t r_sym  = (uint32_t)DL_R_SYM(rela->r_info);

    if (!r_sym || !so->symtab || !so->strtab) return 0;
    dl_sym_t *sym = (dl_sym_t *)(so->symtab + r_sym * (so->syment ? so->syment : sizeof(dl_sym_t)));
    const char *name = (const char *)(so->strtab + sym->st_name);

    uint64_t addr = _lookup_symbol(name, 0);
    if (!addr) {
        _dl_puts("[dynlink] unresolved: "); _dl_puts(name); _dl_puts("\n");
        return 0;
    }

    /* Patch the GOT entry */
    uint64_t *got_slot = (uint64_t *)(so->base + rela->r_offset);
    *got_slot = addr;

    return addr;
}

/* ── Process DT_NEEDED and load dependencies ─────────────────────────── */
static void _load_dependencies(so_info_t *so, const uint8_t *elf_data)
{
    if (!so->strtab || !elf_data) return;

    /* Walk dynamic section to find DT_NEEDED entries */
    /* The dynamic section was processed during _load_elf_mem */
    /* We need to re-scan to get DT_NEEDED (deferred because strtab needed) */

    /* Find PT_DYNAMIC from the ELF headers in memory */
    uint64_t e_phoff = *(uint64_t *)(elf_data + 32);
    uint16_t e_phnum = *(uint16_t *)(elf_data + 56);
    uint16_t e_phentsize = *(uint16_t *)(elf_data + 54);

    for (int i = 0; i < e_phnum; i++) {
        const uint8_t *ph = elf_data + e_phoff + i * e_phentsize;
        if (*(uint32_t *)ph != DL_PT_DYNAMIC) continue;
        uint64_t p_vaddr = *(uint64_t *)(ph + 16);
        uint64_t p_memsz = *(uint64_t *)(ph + 40);
        dl_dyn_t *dyn = (dl_dyn_t *)(so->base + p_vaddr);
        for (size_t j = 0; j * sizeof(dl_dyn_t) < p_memsz; j++) {
            if (dyn[j].d_tag == DL_DT_NULL) break;
            if (dyn[j].d_tag == DL_DT_NEEDED) {
                const char *dep = (const char *)(so->strtab + dyn[j].d_un.d_val);
                _load_library(dep);
            }
        }
        break;
    }
}

/* ── Main entry point called by kernel ──────────────────────────────── */
/*
 * At entry (_dl_start), the stack contains:
 *   [rsp+0]       argc
 *   [rsp+8]       argv[0] ...
 *   ...
 *   [rsp + (argc+1)*8]  0 (NULL)
 *   [rsp + (argc+2)*8]  envp[0] ...
 *   ...  NULL
 *   auxv pairs
 *   AT_NULL, 0
 */
void __attribute__((noreturn)) _dl_start(uint64_t *sp)
{
    /* Parse stack */
    uint64_t argc = sp[0];
    uint64_t *argv = sp + 1;
    uint64_t *envp = argv + argc + 1;

    /* Find auxv */
    uint64_t *auxv_ptr = envp;
    while (*auxv_ptr) auxv_ptr++;
    auxv_ptr++;  /* skip NULL */

    /* Extract key auxv entries */
    uint64_t at_phdr = 0, at_phnum = 0, at_phent = 0;
    uint64_t at_entry = 0, at_base = 0;
    for (dl_auxv_t *av = (dl_auxv_t *)auxv_ptr; av->a_type; av++) {
        switch (av->a_type) {
        case 3:  at_phdr  = av->a_val; break;  /* AT_PHDR */
        case 5:  at_phnum = av->a_val; break;  /* AT_PHNUM */
        case 4:  at_phent = av->a_val; break;  /* AT_PHENT */
        case 9:  at_entry = av->a_val; break;  /* AT_ENTRY */
        case 7:  at_base  = av->a_val; break;  /* AT_BASE (interpreter) */
        }
    }

    _dl_puts("[dynlink] starting dynamic linker\n");

    /* Set up main executable so_info */
    so_info_t *exe = _alloc_lib();
    _memset(exe, 0, sizeof(*exe));
    _strcpy(exe->name, "main");
    exe->base  = 0;   /* ET_EXEC: base is 0 */
    exe->entry = at_entry;

    /* Find and parse the dynamic section of the main executable */
    dl_phdr_t *phdrs = (dl_phdr_t *)at_phdr;
    for (uint64_t i = 0; i < at_phnum; i++) {
        dl_phdr_t *ph = (dl_phdr_t *)((char *)phdrs + i * at_phent);
        if (ph->p_type != DL_PT_DYNAMIC) continue;
        dl_dyn_t *dyn = (dl_dyn_t *)ph->p_vaddr;
        for (;;) {
            if (dyn->d_tag == DL_DT_NULL) break;
            switch (dyn->d_tag) {
            case DL_DT_STRTAB:  exe->strtab   = dyn->d_un.d_ptr; break;
            case DL_DT_SYMTAB:  exe->symtab   = dyn->d_un.d_ptr; break;
            case DL_DT_RELA:    exe->rela      = dyn->d_un.d_ptr; break;
            case DL_DT_RELASZ:  exe->relasz    = (size_t)dyn->d_un.d_val; break;
            case DL_DT_JMPREL:  exe->jmprel    = dyn->d_un.d_ptr; break;
            case DL_DT_PLTRELSZ:exe->jmprelsz  = (size_t)dyn->d_un.d_val; break;
            case DL_DT_PLTGOT:  exe->pltgot    = dyn->d_un.d_ptr; break;
            case DL_DT_INIT:    exe->init       = dyn->d_un.d_val; break;
            case DL_DT_STRSZ:   exe->strsz      = (size_t)dyn->d_un.d_val; break;
            case DL_DT_SYMENT:  exe->syment     = (size_t)dyn->d_un.d_val; break;
            }
            dyn++;
        }
        break;
    }

    /* Load DT_NEEDED libraries */
    if (exe->strtab) {
        dl_phdr_t *phdrs2 = (dl_phdr_t *)at_phdr;
        for (uint64_t i = 0; i < at_phnum; i++) {
            dl_phdr_t *ph = (dl_phdr_t *)((char *)phdrs2 + i * at_phent);
            if (ph->p_type != DL_PT_DYNAMIC) continue;
            dl_dyn_t *dyn = (dl_dyn_t *)ph->p_vaddr;
            for (;;) {
                if (dyn->d_tag == DL_DT_NULL) break;
                if (dyn->d_tag == DL_DT_NEEDED) {
                    const char *name = (const char *)(exe->strtab + dyn->d_un.d_val);
                    _dl_puts("[dynlink] loading dependency: ");
                    _dl_puts(name); _dl_puts("\n");
                    _load_library(name);
                }
                dyn++;
            }
            break;
        }
    }

    /* Apply relocations to all loaded objects */
    for (int i = 0; i < _nlibs; i++) {
        so_info_t *so = &_libs[i];
        if (so->rela    && so->relasz)    _apply_relocations(so, so->rela,    so->relasz);
        if (so->jmprel  && so->jmprelsz)  _apply_relocations(so, so->jmprel,  so->jmprelsz);
        _setup_plt(so);
    }

    /* Call DT_INIT functions (libraries first, then executable) */
    for (int i = _nlibs - 1; i >= 0; i--) {
        if (_libs[i].init && _libs[i].init != (uint64_t)_libs[i].base) {
            ((void (*)(void))_libs[i].init)();
        }
    }

    _dl_puts("[dynlink] jumping to entry point\n");

    /* Jump to executable entry point */
    typedef void __attribute__((noreturn)) (*entry_fn)(uint64_t *);
    entry_fn entry = (entry_fn)at_entry;
    entry(sp);
    __builtin_unreachable();
}

/*
 * _dl_start_asm - actual entry point (before _dl_start C code)
 * This is the _start for the interpreter itself.
 */
__asm__(
    ".global _start\n"
    "_start:\n"
    "    xorq    %rbp, %rbp\n"
    "    movq    %rsp, %rdi\n"   /* pass stack pointer as arg to _dl_start */
    "    andq    $~0xF, %rsp\n"  /* 16-byte align stack */
    "    call    _dl_start\n"
    "    ud2\n"                   /* should never return */
);
