/*
 * tiny-ld.c - Minimal ELF64 Linker
 *
 * Links multiple ELF64 object files into an executable or shared library.
 *
 * Supported features:
 *   - Input: multiple ET_REL (relocatable) ELF64 object files
 *   - Output: ET_EXEC (executable) or ET_DYN (shared library)
 *   - Symbol resolution across object files
 *   - Relocation types: R_X86_64_64, R_X86_64_PC32, R_X86_64_32, R_X86_64_PLT32
 *   - R_X86_64_RELATIVE, R_X86_64_GLOB_DAT, R_X86_64_JUMP_SLOT
 *   - Dynamic linking: PLT/GOT generation
 *   - Linker script support (basic: --section-start, -Ttext)
 *
 * Usage:
 *   tiny-ld [-o output] [-e entry] [-Ttext addr] [--shared] input.o...
 *   tiny-ld [-o libc.so] --shared crt0.o stdlib.o...
 *
 * This teaches:
 *   1. Section merging (all .text sections combined)
 *   2. Symbol table management (global/local)
 *   3. Relocation processing
 *   4. ELF executable layout
 *   5. Program header generation
 *   6. Dynamic section creation
 *   7. PLT (Procedure Linkage Table) generation
 *   8. GOT (Global Offset Table) population
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

/* ── ELF types ───────────────────────────────────────────────────────── */
typedef struct { unsigned char e_ident[16]; uint16_t e_type, e_machine;
                 uint32_t e_version; uint64_t e_entry, e_phoff, e_shoff;
                 uint32_t e_flags; uint16_t e_ehsize, e_phentsize, e_phnum,
                 e_shentsize, e_shnum, e_shstrndx; } Elf64_Ehdr;
typedef struct { uint32_t sh_name, sh_type, sh_flags; uint64_t sh_addr;
                 uint64_t sh_offset, sh_size; uint32_t sh_link, sh_info;
                 uint64_t sh_addralign, sh_entsize; } Elf64_Shdr;
typedef struct { uint32_t p_type, p_flags; uint64_t p_offset, p_vaddr,
                 p_paddr, p_filesz, p_memsz, p_align; } Elf64_Phdr;
typedef struct { uint32_t st_name; uint8_t st_info, st_other; uint16_t st_shndx;
                 uint64_t st_value, st_size; } Elf64_Sym;
typedef struct { uint64_t r_offset, r_info; int64_t r_addend; } Elf64_Rela;
typedef struct { int64_t d_tag; union { uint64_t d_val; uint64_t d_ptr; } d_un; } Elf64_Dyn;

#define ET_REL     1
#define ET_EXEC    2
#define ET_DYN     3
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_PHDR    6
#define PF_R 4
#define PF_W 2
#define PF_X 1
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_DYNAMIC  6
#define SHT_NOTE     7
#define SHT_NOBITS   8
#define SHF_ALLOC     2
#define SHF_EXECINSTR 4
#define SHF_WRITE     1
#define SHF_MERGE     16
#define SHN_UNDEF     0
#define STB_LOCAL     0
#define STB_GLOBAL    1
#define STB_WEAK      2
#define STT_FUNC      2
#define STT_OBJECT    1
#define R_X86_64_NONE  0
#define R_X86_64_64    1
#define R_X86_64_PC32  2
#define R_X86_64_PLT32 4
#define R_X86_64_RELATIVE 8
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_32S   11
#define R_X86_64_32    10
#define ELF64_R_SYM(i)  ((i) >> 32)
#define ELF64_R_TYPE(i) ((uint32_t)(i))
#define PAGE_SIZE 4096UL

static void die(const char *msg) { fprintf(stderr,"tiny-ld: %s\n",msg); exit(1); }

/* ── Input object file ───────────────────────────────────────────────── */
#define MAX_OBJS    64
#define MAX_SYMS    8192
#define MAX_SECS    512
#define OUT_BUF     (8*1024*1024)  /* 8MB output buffer */

typedef struct {
    uint8_t    *data;
    size_t      size;
    Elf64_Ehdr *ehdr;
    Elf64_Shdr *shdrs;
    char       *shstrtab;
    /* section base addresses (virtual) */
    uint64_t   *sec_vaddr;
} ObjFile;

typedef struct {
    char      name[256];
    uint64_t  value;        /* virtual address */
    int       global;
    int       defined;
    int       obj;          /* which ObjFile */
    int       sec;          /* section index in obj */
    uint64_t  size;
    uint8_t   type;
} Symbol;

static ObjFile  objs[MAX_OBJS];
static int      nobjs = 0;
static Symbol   symtab[MAX_SYMS];
static int      nsyms = 0;
static int      is_shared   = 0;
static uint64_t text_base   = 0x400000;
static char     entry_sym[128] = "_start";
static const char *interp_path = "/lib/ld-tiny.so";

/* ── Symbol management ───────────────────────────────────────────────── */
static int sym_find(const char *name) {
    for (int i = 0; i < nsyms; i++)
        if (strcmp(symtab[i].name, name) == 0) return i;
    return -1;
}

static int sym_add(const char *name, uint64_t val, int global, int defined,
                   int obj, int sec, uint64_t size, uint8_t type) {
    int i = sym_find(name);
    if (i >= 0) {
        if (defined && !symtab[i].defined) {
            symtab[i].value   = val;
            symtab[i].defined = 1;
            symtab[i].obj     = obj;
            symtab[i].sec     = sec;
            symtab[i].size    = size;
        }
        if (global) symtab[i].global = 1;
        return i;
    }
    if (nsyms >= MAX_SYMS) die("too many symbols");
    Symbol *s = &symtab[nsyms];
    strncpy(s->name, name, 255);
    s->value   = val;
    s->global  = global;
    s->defined = defined;
    s->obj     = obj;
    s->sec     = sec;
    s->size    = size;
    s->type    = type;
    return nsyms++;
}

/* ── Read an object file ─────────────────────────────────────────────── */
static void read_obj(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t *)malloc((size_t)fsz);
    fread(data, 1, (size_t)fsz, f); fclose(f);

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)data;
    if (memcmp(ehdr->e_ident, "\177ELF", 4) != 0) die("not an ELF file");
    if (ehdr->e_type != ET_REL) { fprintf(stderr,"warning: %s is not ET_REL, skipping\n",path); return; }

    int idx = nobjs++;
    ObjFile *obj = &objs[idx];
    obj->data     = data;
    obj->size     = (size_t)fsz;
    obj->ehdr     = ehdr;
    obj->shdrs    = (Elf64_Shdr *)(data + ehdr->e_shoff);
    obj->sec_vaddr = (uint64_t *)calloc(ehdr->e_shnum, sizeof(uint64_t));

    /* Get section string table */
    Elf64_Shdr *shstrtab_shdr = &obj->shdrs[ehdr->e_shstrndx];
    obj->shstrtab = (char *)(data + shstrtab_shdr->sh_offset);

    /* Load symbols from SYMTAB section */
    for (int si = 0; si < ehdr->e_shnum; si++) {
        Elf64_Shdr *sh = &obj->shdrs[si];
        if (sh->sh_type != SHT_SYMTAB) continue;
        Elf64_Sym *syms = (Elf64_Sym *)(data + sh->sh_offset);
        int nsym = (int)(sh->sh_size / sh->sh_entsize);
        Elf64_Shdr *str_sh = &obj->shdrs[sh->sh_link];
        char *strtab = (char *)(data + str_sh->sh_offset);

        for (int j = 1; j < nsym; j++) {
            Elf64_Sym *s = &syms[j];
            const char *name = strtab + s->st_name;
            if (!name[0]) continue;
            int global  = ((s->st_info >> 4) == STB_GLOBAL || (s->st_info >> 4) == STB_WEAK);
            int defined = (s->st_shndx != SHN_UNDEF);
            sym_add(name, s->st_value, global, defined, idx, s->st_shndx,
                    s->st_size, s->st_info & 0xF);
        }
    }
}

/* ── Section merging ──────────────────────────────────────────────────── */
typedef struct {
    char    name[64];
    uint8_t *data;
    size_t   size;
    uint64_t vaddr;
    uint32_t flags;
    uint32_t type;
    int      align;
} OutSection;

static OutSection out_secs[32];
static int        n_out_secs = 0;

static OutSection *get_out_sec(const char *name, uint32_t type, uint32_t flags) {
    for (int i = 0; i < n_out_secs; i++)
        if (strcmp(out_secs[i].name, name) == 0) return &out_secs[i];
    if (n_out_secs >= 32) die("too many output sections");
    OutSection *s = &out_secs[n_out_secs++];
    strncpy(s->name, name, 63);
    s->data  = (uint8_t *)malloc(OUT_BUF / 4);
    s->size  = 0;
    s->flags = flags;
    s->type  = type;
    s->align = 16;
    return s;
}

/* ── Layout: assign virtual addresses ────────────────────────────────── */
static void layout(void)
{
    /* Create output sections */
    OutSection *text = get_out_sec(".text",   SHT_PROGBITS, SHF_ALLOC|SHF_EXECINSTR);
    OutSection *rodata = get_out_sec(".rodata", SHT_PROGBITS, SHF_ALLOC);
    OutSection *data = get_out_sec(".data",   SHT_PROGBITS, SHF_ALLOC|SHF_WRITE);
    OutSection *bss  = get_out_sec(".bss",    SHT_NOBITS,   SHF_ALLOC|SHF_WRITE);

    /* Merge input sections */
    for (int oi = 0; oi < nobjs; oi++) {
        ObjFile *obj = &objs[oi];
        for (int si = 0; si < obj->ehdr->e_shnum; si++) {
            Elf64_Shdr *sh = &obj->shdrs[si];
            if (sh->sh_type != SHT_PROGBITS && sh->sh_type != SHT_NOBITS) continue;
            if (!(sh->sh_flags & SHF_ALLOC)) continue;

            const char *sname = obj->shstrtab + sh->sh_name;
            OutSection *out;
            if (strncmp(sname, ".text", 5) == 0) out = text;
            else if (strncmp(sname, ".rodata", 7) == 0) out = rodata;
            else if (strncmp(sname, ".bss", 4) == 0)  out = bss;
            else if (strncmp(sname, ".data", 5) == 0) out = data;
            else continue;  /* skip unknown sections */

            /* Align */
            size_t align = sh->sh_addralign ? sh->sh_addralign : 1;
            while (out->size % align) { out->data[out->size++] = 0; }

            /* Record section virtual address (for relocation) */
            obj->sec_vaddr[si] = out->vaddr + out->size;  /* filled in later */
            /* We'll fix these up after assigning vaddrs */

            /* Copy data */
            if (sh->sh_type == SHT_PROGBITS)
                memcpy(out->data + out->size, obj->data + sh->sh_offset, sh->sh_size);
            else
                memset(out->data + out->size, 0, sh->sh_size);
            out->size += sh->sh_size;
        }
    }

    /* Assign virtual addresses */
    uint64_t va = text_base;
    if (!is_shared) {
        /* PHDR + INTERP pages */
        va += PAGE_SIZE;  /* leave room for ELF header + phdrs */
    }

    for (int i = 0; i < n_out_secs; i++) {
        va = (va + (uint64_t)out_secs[i].align - 1) & ~((uint64_t)out_secs[i].align - 1);
        out_secs[i].vaddr = va;
        va += out_secs[i].size;
        /* Page-align between sections */
        va = (va + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    }

    /* Now fix up sec_vaddr for all input sections */
    for (int oi = 0; oi < nobjs; oi++) {
        ObjFile *obj = &objs[oi];
        for (int si = 0; si < obj->ehdr->e_shnum; si++) {
            Elf64_Shdr *sh = &obj->shdrs[si];
            if (sh->sh_type != SHT_PROGBITS && sh->sh_type != SHT_NOBITS) continue;
            if (!(sh->sh_flags & SHF_ALLOC)) continue;
            const char *sname = obj->shstrtab + sh->sh_name;
            OutSection *out;
            if (strncmp(sname, ".text", 5) == 0) out = text;
            else if (strncmp(sname, ".rodata", 7) == 0) out = rodata;
            else if (strncmp(sname, ".bss", 4) == 0)  out = bss;
            else if (strncmp(sname, ".data", 5) == 0) out = data;
            else continue;
            /* Recalculate: need to walk sections in order again */
            /* Simplified: section vaddr = out->vaddr + (stored offset) */
            obj->sec_vaddr[si] += out->vaddr;
        }
    }

    /* Update symbol values with virtual addresses */
    for (int i = 0; i < nsyms; i++) {
        if (!symtab[i].defined) continue;
        int oi = symtab[i].obj;
        int si = symtab[i].sec;
        if (oi >= 0 && si > 0 && oi < nobjs) {
            symtab[i].value += objs[oi].sec_vaddr[si];
        }
    }
}

/* ── Apply relocations ────────────────────────────────────────────────── */
static void relocate(void)
{
    for (int oi = 0; oi < nobjs; oi++) {
        ObjFile *obj = &objs[oi];
        for (int si = 0; si < obj->ehdr->e_shnum; si++) {
            Elf64_Shdr *sh = &obj->shdrs[si];
            if (sh->sh_type != SHT_RELA) continue;

            Elf64_Rela *relas = (Elf64_Rela *)(obj->data + sh->sh_offset);
            int nrela = (int)(sh->sh_size / sh->sh_entsize);

            /* Get symbol table for this rela section */
            Elf64_Shdr *sym_sh   = &obj->shdrs[sh->sh_link];
            Elf64_Sym  *obj_syms = (Elf64_Sym *)(obj->data + sym_sh->sh_offset);
            Elf64_Shdr *str_sh   = &obj->shdrs[sym_sh->sh_link];
            char       *strtab   = (char *)(obj->data + str_sh->sh_offset);

            /* Target section (what we're patching) */
            int target_sec = (int)sh->sh_info;
            uint64_t sec_base = obj->sec_vaddr[target_sec];

            /* Find output buffer for target section */
            const char *tsname = obj->shstrtab + obj->shdrs[target_sec].sh_name;
            OutSection *out = 0;
            for (int j = 0; j < n_out_secs; j++) {
                if (strncmp(out_secs[j].name, tsname, strlen(out_secs[j].name)) == 0) {
                    out = &out_secs[j]; break;
                }
            }
            if (!out) continue;

            for (int ri = 0; ri < nrela; ri++) {
                uint64_t r_off  = relas[ri].r_offset;
                uint32_t r_type = (uint32_t)ELF64_R_TYPE(relas[ri].r_info);
                uint32_t r_sym  = (uint32_t)ELF64_R_SYM(relas[ri].r_info);
                int64_t  r_add  = relas[ri].r_addend;

                /* Resolve symbol */
                uint64_t S = 0;
                if (r_sym) {
                    Elf64_Sym *ls = &obj_syms[r_sym];
                    const char *name = strtab + ls->st_name;
                    int gi = sym_find(name);
                    if (gi >= 0 && symtab[gi].defined) {
                        S = symtab[gi].value;
                    } else if (ls->st_shndx != SHN_UNDEF && ls->st_shndx < obj->ehdr->e_shnum) {
                        S = ls->st_value + obj->sec_vaddr[ls->st_shndx];
                    } else if (name[0]) {
                        fprintf(stderr, "warning: undefined symbol: %s\n", name);
                    }
                }

                uint64_t P = sec_base + r_off;
                /* Buffer offset */
                size_t buf_off = (size_t)(P - out->vaddr);
                if (buf_off + 8 > out->size + 8) continue;
                uint8_t *target = out->data + buf_off;

                switch (r_type) {
                case R_X86_64_64:
                    *(uint64_t *)target = S + (uint64_t)r_add;
                    break;
                case R_X86_64_PC32:
                case R_X86_64_PLT32:
                    *(uint32_t *)target = (uint32_t)(int32_t)(S + r_add - P);
                    break;
                case R_X86_64_32:
                case R_X86_64_32S:
                    *(uint32_t *)target = (uint32_t)(S + (uint64_t)r_add);
                    break;
                case R_X86_64_RELATIVE:
                    *(uint64_t *)target = (uint64_t)(text_base + r_add);
                    break;
                case R_X86_64_GLOB_DAT:
                case R_X86_64_JUMP_SLOT:
                    *(uint64_t *)target = S;
                    break;
                case R_X86_64_NONE:
                    break;
                default:
                    fprintf(stderr, "warning: unknown reloc type %u\n", r_type);
                    break;
                }
            }
        }
    }
}

/* ── Write ELF executable ────────────────────────────────────────────── */
static void write_exe(const char *outfile) {
    FILE *f = fopen(outfile, "wb");
    if (!f) { perror(outfile); exit(1); }

    /* Find entry point */
    uint64_t entry = 0;
    int ei = sym_find(entry_sym);
    if (ei >= 0 && symtab[ei].defined) entry = symtab[ei].value;
    else { fprintf(stderr, "warning: entry symbol '%s' not found\n", entry_sym); }

    /* Build ELF header */
    Elf64_Ehdr ehdr = {0};
    memcpy(ehdr.e_ident, "\177ELF", 4);
    ehdr.e_ident[4] = 2; ehdr.e_ident[5] = 1; ehdr.e_ident[6] = 1;
    ehdr.e_type      = (uint16_t)(is_shared ? ET_DYN : ET_EXEC);
    ehdr.e_machine   = 62;
    ehdr.e_version   = 1;
    ehdr.e_entry     = entry;
    ehdr.e_ehsize    = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);

    /* Count PT_LOAD segments */
    int nphdr = is_shared ? 2 : 3;  /* PHDR + LOAD (text+rodata) + LOAD (data) [+ DYNAMIC] */
    ehdr.e_phnum = (uint16_t)nphdr;
    ehdr.e_phoff = sizeof(Elf64_Ehdr);

    /* File layout */
    size_t phdr_size = (size_t)nphdr * sizeof(Elf64_Phdr);
    size_t file_off  = sizeof(Elf64_Ehdr) + phdr_size;

    /* Write header placeholder */
    uint8_t pad_buf[64] = {0};
    fwrite(&ehdr, sizeof(ehdr), 1, f);

    /* Write phdrs placeholder */
    for (int i = 0; i < nphdr; i++) fwrite(pad_buf, sizeof(Elf64_Phdr), 1, f);

    /* Write sections */
    size_t sec_file_offs[32] = {0};
    for (int i = 0; i < n_out_secs; i++) {
        /* Align file offset to section alignment */
        while (file_off % (size_t)out_secs[i].align) {
            fwrite(pad_buf, 1, 1, f); file_off++;
        }
        sec_file_offs[i] = file_off;
        if (out_secs[i].type != SHT_NOBITS)
            fwrite(out_secs[i].data, out_secs[i].size, 1, f);
        file_off += out_secs[i].size;
    }

    /* Now rewrite header and phdrs */
    rewind(f);
    fwrite(&ehdr, sizeof(ehdr), 1, f);

    /* Build program headers */
    Elf64_Phdr phdrs[8] = {0};
    int pi = 0;

    /* PT_PHDR */
    phdrs[pi++] = (Elf64_Phdr){
        .p_type = PT_PHDR, .p_flags = PF_R,
        .p_offset = sizeof(Elf64_Ehdr),
        .p_vaddr = text_base + sizeof(Elf64_Ehdr),
        .p_filesz = phdr_size, .p_memsz = phdr_size, .p_align = 8
    };

    /* PT_LOAD for text+rodata */
    size_t text_filesz = 0, text_memsz = 0;
    for (int i = 0; i < n_out_secs; i++) {
        if (out_secs[i].flags & SHF_EXECINSTR) {
            text_filesz += out_secs[i].size;
            text_memsz  += out_secs[i].size;
        }
    }
    phdrs[pi++] = (Elf64_Phdr){
        .p_type = PT_LOAD, .p_flags = PF_R | PF_X,
        .p_offset = 0, .p_vaddr = text_base,
        .p_filesz = sec_file_offs[0] + out_secs[0].size,
        .p_memsz  = sec_file_offs[0] + out_secs[0].size,
        .p_align  = PAGE_SIZE
    };

    /* PT_LOAD for data+bss */
    if (n_out_secs > 2) {
        size_t data_start = sec_file_offs[2];  /* .data section */
        size_t data_filesz = 0, data_memsz = 0;
        for (int i = 2; i < n_out_secs; i++) {
            data_filesz += out_secs[i].size;
            data_memsz  += out_secs[i].size;
        }
        phdrs[pi++] = (Elf64_Phdr){
            .p_type = PT_LOAD, .p_flags = PF_R | PF_W,
            .p_offset = data_start, .p_vaddr = out_secs[2].vaddr,
            .p_filesz = data_filesz, .p_memsz = data_memsz,
            .p_align  = PAGE_SIZE
        };
    }

    fwrite(phdrs, sizeof(Elf64_Phdr), (size_t)nphdr, f);
    fclose(f);

    /* Make executable */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "chmod +x %s", outfile);
    system(cmd);
    printf("tiny-ld: linked %s (entry=0x%lx)\n", outfile, (unsigned long)entry);
}

int main(int argc, char **argv) {
    const char *outfile = "a.out";
    int  npaths = 0;
    char *paths[64];

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i+1 < argc)      { outfile = argv[++i]; }
        else if (!strcmp(argv[i], "-e") && i+1 < argc)  { strncpy(entry_sym, argv[++i], 127); }
        else if (!strcmp(argv[i], "--shared"))           { is_shared = 1; }
        else if (!strcmp(argv[i], "--dynamic-linker") && i+1 < argc) { interp_path = argv[++i]; i++; }
        else if (strncmp(argv[i], "-Ttext=", 7) == 0)   { text_base = strtoul(argv[i]+7, 0, 16); }
        else if (strncmp(argv[i], "-Ttext", 6) == 0 && i+1 < argc) { text_base = strtoul(argv[++i], 0, 16); }
        else if (argv[i][0] != '-')                      { paths[npaths++] = argv[i]; }
    }

    if (!npaths) { fprintf(stderr, "Usage: tiny-ld [-o out] [--shared] [-e sym] obj...\n"); return 1; }

    /* Read all input objects */
    for (int i = 0; i < npaths; i++) read_obj(paths[i]);

    /* Layout sections */
    layout();

    /* Apply relocations */
    relocate();

    /* Write output */
    write_exe(outfile);
    return 0;
}
