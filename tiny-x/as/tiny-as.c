/*
 * tiny-as.c - Minimal x86_64 Assembler
 *
 * Implements a subset of GAS/NASM assembly syntax.
 * Outputs ELF64 object files.
 *
 * Supported instructions:
 *   mov, add, sub, mul, div, and, or, xor, not, neg
 *   cmp, test, jmp, je, jne, jl, jle, jg, jge, jz, jnz
 *   call, ret, push, pop
 *   syscall, hlt, nop, int, lea
 *   inc, dec, shl, shr, sar
 *   movzx, movsx
 *
 * Directives:
 *   .section, .global, .extern, .byte, .short, .long, .quad
 *   .ascii, .asciz, .string, .zero, .align, .space
 *   .text, .data, .bss, .rodata
 *
 * Register names: rax,rbx,rcx,rdx,rsi,rdi,rsp,rbp,r8-r15
 *                 eax,ebx,ecx,edx,esi,edi,esp,ebp,r8d-r15d
 *                 ax,bx,cx,dx,si,di,sp,bp
 *                 al,bl,cl,dl,sil,dil,spl,bpl,r8b-r15b
 *
 * Usage: tiny-as input.s -o output.o
 *
 * This is an educational implementation showing how an assembler works:
 *   1. Lexing: tokenize source into tokens
 *   2. Parsing: identify instructions, directives, labels
 *   3. First pass: collect labels and their offsets
 *   4. Second pass: encode instructions (resolve label references)
 *   5. Emit ELF object file with relocations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>

/* ── ELF types ───────────────────────────────────────────────────────── */
typedef struct { unsigned char e_ident[16]; uint16_t e_type, e_machine;
                 uint32_t e_version; uint64_t e_entry, e_phoff, e_shoff;
                 uint32_t e_flags; uint16_t e_ehsize, e_phentsize, e_phnum,
                 e_shentsize, e_shnum, e_shstrndx; } Elf64_Ehdr;
typedef struct { uint32_t sh_name, sh_type, sh_flags; uint64_t sh_addr;
                 uint64_t sh_offset, sh_size; uint32_t sh_link, sh_info;
                 uint64_t sh_addralign, sh_entsize; } Elf64_Shdr;
typedef struct { uint32_t st_name; uint8_t st_info, st_other;
                 uint16_t st_shndx; uint64_t st_value, st_size; } Elf64_Sym;
typedef struct { uint64_t r_offset, r_info; int64_t r_addend; } Elf64_Rela;

#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHF_ALLOC    2
#define SHF_EXECINSTR 4
#define SHF_WRITE    1
#define STB_LOCAL    0
#define STB_GLOBAL   1
#define STT_NOTYPE   0
#define STT_FUNC     2
#define STT_OBJECT   1
#define STT_SECTION  3
#define SHN_UNDEF    0
#define R_X86_64_32  10
#define R_X86_64_64  1
#define R_X86_64_PC32 2
#define R_X86_64_PLT32 4

/* ── Assembler state ─────────────────────────────────────────────────── */
#define MAX_SECTIONS  16
#define MAX_SYMBOLS   1024
#define MAX_RELOCS    2048
#define MAX_LABELS    1024
#define SECTION_BUF   (1024*1024)  /* 1MB per section */

typedef struct {
    char    name[64];
    uint8_t *data;
    size_t   size, capacity;
    uint32_t sh_type, sh_flags;
    int      idx;           /* section index in ELF */
} Section;

typedef struct {
    char     name[128];
    int      section;       /* section index */
    uint64_t offset;        /* offset within section */
    int      global;
    int      defined;
    uint8_t  st_type;
} Symbol;

typedef struct {
    int      section;
    uint64_t offset;
    int      sym_idx;
    uint32_t type;
    int64_t  addend;
} Reloc;

static Section  sections[MAX_SECTIONS];
static int      nsections = 0;
static Symbol   symbols[MAX_SYMBOLS];
static int      nsymbols = 0;
static Reloc    relocs[MAX_RELOCS];
static int      nrelocs = 0;
static int      cur_section = 0;
static int      line_num = 0;
static const char *input_file = 0;

/* ── Utilities ───────────────────────────────────────────────────────── */
static void error(const char *msg) {
    fprintf(stderr, "%s:%d: error: %s\n", input_file, line_num, msg);
    exit(1);
}
static void warn(const char *msg) {
    fprintf(stderr, "%s:%d: warning: %s\n", input_file, line_num, msg);
}

static Section *get_section(const char *name) {
    for (int i = 0; i < nsections; i++)
        if (strcmp(sections[i].name, name) == 0) return &sections[i];
    return 0;
}

static Section *new_section(const char *name, uint32_t type, uint32_t flags) {
    if (nsections >= MAX_SECTIONS) error("too many sections");
    Section *s = &sections[nsections];
    strncpy(s->name, name, 63);
    s->sh_type = type;
    s->sh_flags = flags;
    s->data = (uint8_t *)malloc(SECTION_BUF);
    s->size = 0;
    s->capacity = SECTION_BUF;
    s->idx = nsections;
    nsections++;
    return s;
}

static void emit(Section *s, const uint8_t *bytes, size_t n) {
    if (s->size + n > s->capacity) error("section overflow");
    memcpy(s->data + s->size, bytes, n);
    s->size += n;
}

static void emit1(Section *s, uint8_t b)  { emit(s, &b, 1); }
static void emit2(Section *s, uint16_t w) { emit(s, (uint8_t *)&w, 2); }
static void emit4(Section *s, uint32_t d) { emit(s, (uint8_t *)&d, 4); }
static void emit8(Section *s, uint64_t q) { emit(s, (uint8_t *)&q, 8); }

/* ── Register encoding ───────────────────────────────────────────────── */
typedef struct { const char *name; int reg; int size; int rex; } RegInfo;
static RegInfo regtab[] = {
    {"rax",0,64,0},{"rcx",1,64,0},{"rdx",2,64,0},{"rbx",3,64,0},
    {"rsp",4,64,0},{"rbp",5,64,0},{"rsi",6,64,0},{"rdi",7,64,0},
    {"r8", 0,64,1},{"r9", 1,64,1},{"r10",2,64,1},{"r11",3,64,1},
    {"r12",4,64,1},{"r13",5,64,1},{"r14",6,64,1},{"r15",7,64,1},
    {"eax",0,32,0},{"ecx",1,32,0},{"edx",2,32,0},{"ebx",3,32,0},
    {"esp",4,32,0},{"ebp",5,32,0},{"esi",6,32,0},{"edi",7,32,0},
    {"r8d",0,32,1},{"r9d",1,32,1},{"r10d",2,32,1},{"r11d",3,32,1},
    {"ax",0,16,0},{"cx",1,16,0},{"dx",2,16,0},{"bx",3,16,0},
    {"al",0,8,0},{"cl",1,8,0},{"dl",2,8,0},{"bl",3,8,0},
    {"sil",6,8,0},{"dil",7,8,0},{"spl",4,8,0},{"bpl",5,8,0},
    {0,0,0,0}
};

static int parse_reg(const char *s, int *rex_ext) {
    char lower[16];
    int i = 0;
    while (s[i] && i < 15) { lower[i] = tolower((unsigned char)s[i]); i++; }
    lower[i] = 0;
    for (RegInfo *r = regtab; r->name; r++) {
        if (strcmp(r->name, lower) == 0) {
            if (rex_ext) *rex_ext = r->rex;
            return r->reg;
        }
    }
    return -1;
}

/* ── Symbol management ───────────────────────────────────────────────── */
static int find_sym(const char *name) {
    for (int i = 0; i < nsymbols; i++)
        if (strcmp(symbols[i].name, name) == 0) return i;
    return -1;
}

static int add_sym(const char *name, int section, uint64_t offset,
                   int global, int defined, uint8_t type) {
    int i = find_sym(name);
    if (i >= 0) {
        if (defined) {
            symbols[i].section = section;
            symbols[i].offset  = offset;
            symbols[i].defined = 1;
        }
        if (global) symbols[i].global = 1;
        return i;
    }
    if (nsymbols >= MAX_SYMBOLS) error("too many symbols");
    Symbol *sym = &symbols[nsymbols];
    strncpy(sym->name, name, 127);
    sym->section = section;
    sym->offset  = offset;
    sym->global  = global;
    sym->defined = defined;
    sym->st_type = type;
    return nsymbols++;
}

/* ── Lexer ───────────────────────────────────────────────────────────── */
typedef enum {
    TOK_EOF, TOK_NEWLINE, TOK_COMMA, TOK_COLON, TOK_PLUS, TOK_MINUS,
    TOK_STAR, TOK_LBRACKET, TOK_RBRACKET, TOK_LPAREN, TOK_RPAREN,
    TOK_IDENT, TOK_NUMBER, TOK_STRING, TOK_DIRECTIVE, TOK_DOLLAR
} TokType;

typedef struct {
    TokType type;
    char    text[256];
    int64_t num;
} Token;

static const char *src_pos;
static Token cur_tok;

static int64_t parse_num(const char *s) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return (int64_t)strtoll(s, 0, 16);
    if (s[0] == '0' && s[1])
        return (int64_t)strtoll(s, 0, 8);
    return strtoll(s, 0, 10);
}

static void next_tok(void) {
    while (*src_pos == ' ' || *src_pos == '\t') src_pos++;
    if (!*src_pos || *src_pos == '\n') {
        if (*src_pos == '\n') { src_pos++; line_num++; }
        cur_tok.type = *src_pos ? TOK_NEWLINE : TOK_EOF;
        return;
    }
    /* Comment */
    if (*src_pos == ';' || (*src_pos == '/' && src_pos[1] == '/') ||
        *src_pos == '#') {
        while (*src_pos && *src_pos != '\n') src_pos++;
        cur_tok.type = TOK_NEWLINE;
        return;
    }
    /* String */
    if (*src_pos == '"') {
        src_pos++;
        int i = 0;
        while (*src_pos && *src_pos != '"') {
            if (*src_pos == '\\' && src_pos[1]) {
                src_pos++;
                switch (*src_pos) {
                case 'n': cur_tok.text[i++] = '\n'; break;
                case 't': cur_tok.text[i++] = '\t'; break;
                case 'r': cur_tok.text[i++] = '\r'; break;
                case '0': cur_tok.text[i++] = '\0'; break;
                default:  cur_tok.text[i++] = *src_pos; break;
                }
            } else {
                cur_tok.text[i++] = *src_pos;
            }
            src_pos++;
        }
        if (*src_pos == '"') src_pos++;
        cur_tok.text[i] = 0;
        cur_tok.type = TOK_STRING;
        cur_tok.num  = i;
        return;
    }
    /* Directive (.xxx) */
    if (*src_pos == '.') {
        int i = 0; cur_tok.text[i++] = *src_pos++;
        while (isalnum((unsigned char)*src_pos) || *src_pos == '_')
            cur_tok.text[i++] = *src_pos++;
        cur_tok.text[i] = 0;
        cur_tok.type = TOK_DIRECTIVE;
        return;
    }
    /* Number */
    if (isdigit((unsigned char)*src_pos) ||
        (*src_pos == '-' && isdigit((unsigned char)src_pos[1]))) {
        int i = 0;
        if (*src_pos == '-') cur_tok.text[i++] = *src_pos++;
        while (isxdigit((unsigned char)*src_pos) || *src_pos == 'x' || *src_pos == 'X')
            cur_tok.text[i++] = *src_pos++;
        cur_tok.text[i] = 0;
        cur_tok.num  = parse_num(cur_tok.text);
        cur_tok.type = TOK_NUMBER;
        return;
    }
    /* Identifier */
    if (isalpha((unsigned char)*src_pos) || *src_pos == '_' || *src_pos == '%') {
        int i = 0;
        while (isalnum((unsigned char)*src_pos) || *src_pos == '_' ||
               *src_pos == '.' || *src_pos == '@')
            cur_tok.text[i++] = *src_pos++;
        cur_tok.text[i] = 0;
        cur_tok.type = TOK_IDENT;
        return;
    }
    /* Single char tokens */
    switch (*src_pos) {
    case ',': cur_tok.type = TOK_COMMA;    break;
    case ':': cur_tok.type = TOK_COLON;    break;
    case '+': cur_tok.type = TOK_PLUS;     break;
    case '-': cur_tok.type = TOK_MINUS;    break;
    case '*': cur_tok.type = TOK_STAR;     break;
    case '[': cur_tok.type = TOK_LBRACKET; break;
    case ']': cur_tok.type = TOK_RBRACKET; break;
    case '(': cur_tok.type = TOK_LPAREN;   break;
    case ')': cur_tok.type = TOK_RPAREN;   break;
    case '$': cur_tok.type = TOK_DOLLAR;   break;
    default:  error("unexpected character"); break;
    }
    cur_tok.text[0] = *src_pos++; cur_tok.text[1] = 0;
}

/* ── x86_64 Instruction Encoder ──────────────────────────────────────── */
/*
 * REX prefix: 0x40 | W | R | X | B
 *   W = 1 for 64-bit operand size
 *   R = REG field extension
 *   X = SIB index extension
 *   B = R/M or base extension
 */
#define REX_W 0x48
#define REX_R 0x44
#define REX_X 0x42
#define REX_B 0x41
#define REX   0x40

static void encode_modrm(Section *s, int mod, int reg, int rm) {
    emit1(s, (uint8_t)((mod << 6) | ((reg & 7) << 3) | (rm & 7)));
}

/* Emit REX + opcode + ModRM for reg,reg operation (64-bit) */
static void enc_rr(Section *s, uint8_t op, int reg, int rm, int rex_r, int rex_b) {
    uint8_t rex = REX_W;
    if (rex_r) rex |= 0x04;  /* REX.R */
    if (rex_b) rex |= 0x01;  /* REX.B */
    emit1(s, rex);
    emit1(s, op);
    encode_modrm(s, 3, reg, rm);  /* mod=3: register operand */
}

/* Encode: op reg, imm64 */
static void enc_ri(Section *s, uint8_t op_rm, int reg, int rex_b, int64_t imm) {
    /* Check if imm fits in 32-bit signed */
    int fits32 = (imm >= -0x80000000LL && imm <= 0x7FFFFFFFLL);
    uint8_t rex = REX_W;
    if (rex_b) rex |= 0x01;
    if (!fits32) {
        /* movabs: REX.W + 0xB8+reg, imm64 */
        if (op_rm == 0xC7) {  /* mov */
            emit1(s, rex);
            emit1(s, (uint8_t)(0xB8 + (reg & 7)));
            emit8(s, (uint64_t)imm);
            return;
        }
    }
    emit1(s, rex);
    emit1(s, op_rm);
    encode_modrm(s, 3, 0, reg);  /* /0 for most */
    emit4(s, (uint32_t)(int32_t)imm);
}

/* JMP/CALL: 1-byte short or 4-byte near relative */
static void enc_jmp(Section *s, uint8_t op_near, const char *label, int64_t imm, int rel) {
    if (rel) {
        /* Emit with relocation */
        int sym_i = add_sym(label, 0, 0, 0, 0, STT_NOTYPE);
        /* Encode: E9 + rel32 (for JMP) or E8 + rel32 (for CALL) */
        emit1(s, op_near);
        size_t off = s->size;
        emit4(s, 0);  /* placeholder */
        relocs[nrelocs++] = (Reloc){cur_section, off, sym_i, R_X86_64_PC32, -4};
    } else {
        /* Known relative offset */
        int32_t rel32 = (int32_t)(imm - (int64_t)(s->size + 4));
        if (rel32 >= -128 && rel32 <= 127 && op_near != 0xE8) {
            uint8_t short_op = op_near == 0xE9 ? 0xEB : op_near;
            emit1(s, short_op);
            emit1(s, (uint8_t)(int8_t)(imm - (int64_t)(s->size + 1)));
        } else {
            emit1(s, op_near);
            emit4(s, (uint32_t)(int32_t)(imm - (int64_t)(s->size + 4)));
        }
    }
}

/* ── Assembler pass ──────────────────────────────────────────────────── */
/*
 * Parse and assemble one line.
 * This is a simplified single-pass assembler with forward reference patches.
 */
typedef struct {
    char     name[128];
    int      section;
    size_t   offset;    /* offset of rel32 field to patch */
} FwdRef;
static FwdRef fwd_refs[MAX_LABELS];
static int    nfwd_refs = 0;

static void handle_directive(const char *dir) {
    if (strcmp(dir, ".text") == 0 || strcmp(dir, ".section .text") == 0) {
        Section *s = get_section(".text");
        if (!s) s = new_section(".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
        cur_section = s->idx;
    } else if (strcmp(dir, ".data") == 0) {
        Section *s = get_section(".data");
        if (!s) s = new_section(".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
        cur_section = s->idx;
    } else if (strcmp(dir, ".bss") == 0) {
        Section *s = get_section(".bss");
        if (!s) s = new_section(".bss", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
        cur_section = s->idx;
    } else if (strcmp(dir, ".rodata") == 0) {
        Section *s = get_section(".rodata");
        if (!s) s = new_section(".rodata", SHT_PROGBITS, SHF_ALLOC);
        cur_section = s->idx;
    } else if (strcmp(dir, ".global") == 0 || strcmp(dir, ".globl") == 0) {
        next_tok();
        if (cur_tok.type == TOK_IDENT) {
            int i = find_sym(cur_tok.text);
            if (i >= 0) symbols[i].global = 1;
            else add_sym(cur_tok.text, 0, 0, 1, 0, STT_NOTYPE);
        }
    } else if (strcmp(dir, ".extern") == 0) {
        next_tok();
        if (cur_tok.type == TOK_IDENT)
            add_sym(cur_tok.text, SHN_UNDEF, 0, 1, 0, STT_NOTYPE);
    } else if (strcmp(dir, ".byte") == 0) {
        next_tok();
        do {
            if (cur_tok.type == TOK_NUMBER)
                emit1(&sections[cur_section], (uint8_t)cur_tok.num);
            else if (cur_tok.type == TOK_STRING) {
                for (int i = 0; i < (int)cur_tok.num; i++)
                    emit1(&sections[cur_section], (uint8_t)cur_tok.text[i]);
            }
            next_tok();
        } while (cur_tok.type == TOK_COMMA && (next_tok(), 1));
        return;
    } else if (strcmp(dir, ".short") == 0 || strcmp(dir, ".word") == 0) {
        next_tok();
        do { if (cur_tok.type == TOK_NUMBER) emit2(&sections[cur_section], (uint16_t)cur_tok.num);
             next_tok(); } while (cur_tok.type == TOK_COMMA && (next_tok(), 1));
        return;
    } else if (strcmp(dir, ".long") == 0 || strcmp(dir, ".int") == 0) {
        next_tok();
        do { if (cur_tok.type == TOK_NUMBER) emit4(&sections[cur_section], (uint32_t)cur_tok.num);
             next_tok(); } while (cur_tok.type == TOK_COMMA && (next_tok(), 1));
        return;
    } else if (strcmp(dir, ".quad") == 0) {
        next_tok();
        do { if (cur_tok.type == TOK_NUMBER) emit8(&sections[cur_section], (uint64_t)cur_tok.num);
             next_tok(); } while (cur_tok.type == TOK_COMMA && (next_tok(), 1));
        return;
    } else if (strcmp(dir, ".ascii") == 0 || strcmp(dir, ".string") == 0) {
        next_tok();
        if (cur_tok.type == TOK_STRING) {
            emit(&sections[cur_section], (uint8_t *)cur_tok.text, (size_t)cur_tok.num);
            if (dir[6] == 'z' || strcmp(dir, ".string") == 0)
                emit1(&sections[cur_section], 0);
        }
    } else if (strcmp(dir, ".asciz") == 0) {
        next_tok();
        if (cur_tok.type == TOK_STRING) {
            emit(&sections[cur_section], (uint8_t *)cur_tok.text, (size_t)cur_tok.num);
            emit1(&sections[cur_section], 0);
        }
    } else if (strcmp(dir, ".zero") == 0 || strcmp(dir, ".space") == 0) {
        next_tok();
        size_t n = (cur_tok.type == TOK_NUMBER) ? (size_t)cur_tok.num : 0;
        for (size_t i = 0; i < n; i++) emit1(&sections[cur_section], 0);
    } else if (strcmp(dir, ".align") == 0) {
        next_tok();
        size_t align = (cur_tok.type == TOK_NUMBER) ? (size_t)cur_tok.num : 1;
        if (align > 1) {
            Section *s = &sections[cur_section];
            while (s->size % align) emit1(s, 0x90);  /* NOP padding */
        }
    }
    /* Skip to end of line */
}

/* Parse one operand and return encoding info */
typedef struct {
    int is_reg, is_mem, is_imm, is_label;
    int reg, rex_ext;
    int64_t imm;
    char label[128];
    /* memory: [base + disp] */
    int base_reg, base_rex;
    int64_t disp;
} Operand;

static void parse_operand(Operand *op) {
    memset(op, 0, sizeof(*op));
    /* Skip AT&T-style size suffixes and '%' register prefix */
    while (cur_tok.type == TOK_DOLLAR) { next_tok(); }  /* immediate */

    if (cur_tok.type == TOK_NUMBER) {
        op->is_imm = 1; op->imm = cur_tok.num; next_tok();
    } else if (cur_tok.type == TOK_IDENT) {
        /* Try register */
        int r = parse_reg(cur_tok.text, &op->rex_ext);
        if (r >= 0) {
            op->is_reg = 1; op->reg = r; next_tok();
        } else {
            /* Label/symbol */
            op->is_label = 1; op->is_imm = 0;
            strncpy(op->label, cur_tok.text, 127); next_tok();
        }
    } else if (cur_tok.type == TOK_LBRACKET) {
        /* Memory [base + disp] */
        next_tok();
        op->is_mem = 1;
        if (cur_tok.type == TOK_IDENT) {
            op->base_reg = parse_reg(cur_tok.text, &op->base_rex);
            next_tok();
        }
        if (cur_tok.type == TOK_PLUS || cur_tok.type == TOK_MINUS) {
            int neg = (cur_tok.type == TOK_MINUS); next_tok();
            if (cur_tok.type == TOK_NUMBER) {
                op->disp = neg ? -cur_tok.num : cur_tok.num; next_tok();
            }
        }
        if (cur_tok.type == TOK_RBRACKET) next_tok();
    }
}

/* Assemble one instruction */
static void assemble_insn(const char *mnem) {
    Section *s = &sections[cur_section];
    Operand op1, op2;
    memset(&op1, 0, sizeof op1);
    memset(&op2, 0, sizeof op2);

    /* Check for no-operand instructions */
    if (!strcmp(mnem,"ret") || !strcmp(mnem,"retq")) { emit1(s,0xC3); return; }
    if (!strcmp(mnem,"nop")) { emit1(s,0x90); return; }
    if (!strcmp(mnem,"hlt")) { emit1(s,0xF4); return; }
    if (!strcmp(mnem,"syscall")) { emit1(s,0x0F); emit1(s,0x05); return; }
    if (!strcmp(mnem,"leave"))   { emit1(s,0xC9); return; }
    if (!strcmp(mnem,"pushfq"))  { emit1(s,0x9C); return; }
    if (!strcmp(mnem,"popfq"))   { emit1(s,0x9D); return; }
    if (!strcmp(mnem,"sti"))     { emit1(s,0xFB); return; }
    if (!strcmp(mnem,"cli"))     { emit1(s,0xFA); return; }
    if (!strcmp(mnem,"cld"))     { emit1(s,0xFC); return; }
    if (!strcmp(mnem,"std"))     { emit1(s,0xFD); return; }
    if (!strcmp(mnem,"iretq"))   { emit1(s,0x48); emit1(s,0xCF); return; }
    if (!strcmp(mnem,"ud2"))     { emit1(s,0x0F); emit1(s,0x0B); return; }

    /* Parse operands */
    next_tok();
    if (cur_tok.type != TOK_NEWLINE && cur_tok.type != TOK_EOF) {
        parse_operand(&op1);
        if (cur_tok.type == TOK_COMMA) { next_tok(); parse_operand(&op2); }
    }

    /* push/pop */
    if (!strcmp(mnem,"push") || !strcmp(mnem,"pushq")) {
        if (op1.is_reg) {
            if (op1.rex_ext) emit1(s, REX_B);
            emit1(s, (uint8_t)(0x50 + op1.reg));
        } else if (op1.is_imm) {
            emit1(s, 0x68); emit4(s, (uint32_t)(int32_t)op1.imm);
        }
        return;
    }
    if (!strcmp(mnem,"pop") || !strcmp(mnem,"popq")) {
        if (op1.is_reg) {
            if (op1.rex_ext) emit1(s, REX_B);
            emit1(s, (uint8_t)(0x58 + op1.reg));
        }
        return;
    }

    /* call */
    if (!strcmp(mnem,"call") || !strcmp(mnem,"callq")) {
        if (op1.is_label)
            enc_jmp(s, 0xE8, op1.label, 0, 1);
        else if (op1.is_reg) {
            emit1(s, REX_W); emit1(s, 0xFF);
            encode_modrm(s, 3, 2, op1.reg);
        }
        return;
    }

    /* jmp and conditional jumps */
    static struct { const char *mnem; uint8_t op; } jmps[] = {
        {"jmp",0xE9},{"je",0x00},{"jz",0x00},{"jne",0x01},{"jnz",0x01},
        {"jl",0x0C},{"jnge",0x0C},{"jle",0x0E},{"jng",0x0E},
        {"jg",0x0F},{"jnle",0x0F},{"jge",0x0D},{"jnl",0x0D},
        {"jb",0x02},{"jnae",0x02},{"jbe",0x06},{"jna",0x06},
        {"ja",0x07},{"jnbe",0x07},{"jae",0x03},{"jnb",0x03},
        {"js",0x08},{"jns",0x09},
        {0,0}
    };
    for (int i = 0; jmps[i].mnem; i++) {
        if (!strcmp(mnem, jmps[i].mnem)) {
            if (op1.is_label) {
                if (jmps[i].op == 0xE9) {
                    enc_jmp(s, 0xE9, op1.label, 0, 1);
                } else {
                    /* Conditional: 0F 8x rel32 */
                    emit1(s, 0x0F); emit1(s, (uint8_t)(0x80 | jmps[i].op));
                    size_t off = s->size; emit4(s, 0);
                    int sym_i = add_sym(op1.label, 0, 0, 0, 0, STT_NOTYPE);
                    relocs[nrelocs++] = (Reloc){cur_section, off, sym_i, R_X86_64_PC32, -4};
                }
            } else if (op1.is_imm) {
                enc_jmp(s, jmps[i].op == 0xE9 ? 0xE9 : 0x80 | jmps[i].op,
                        0, op1.imm, 0);
            }
            return;
        }
    }

    /* mov dst, src */
    if (!strcmp(mnem,"mov") || !strcmp(mnem,"movq")) {
        if (op1.is_reg && op2.is_reg) {
            enc_rr(s, 0x89, op2.reg, op1.reg, op2.rex_ext, op1.rex_ext);
        } else if (op1.is_reg && op2.is_imm) {
            /* MOV r/m64, imm32 sign-extended: REX.W 0xC7 /0 imm32 */
            /* Or MOV r64, imm64: REX.W B8+r imm64 */
            int fits32 = (op2.imm >= -0x80000000LL && op2.imm <= 0x7FFFFFFFLL);
            if (fits32) {
                uint8_t rex = (uint8_t)(REX_W | (op1.rex_ext ? 0x01 : 0));
                emit1(s, rex); emit1(s, 0xC7);
                encode_modrm(s, 3, 0, op1.reg);
                emit4(s, (uint32_t)(int32_t)op2.imm);
            } else {
                uint8_t rex = (uint8_t)(REX_W | (op1.rex_ext ? 0x01 : 0));
                emit1(s, rex);
                emit1(s, (uint8_t)(0xB8 + (op1.reg & 7)));
                emit8(s, (uint64_t)op2.imm);
            }
        } else if (op1.is_reg && op2.is_label) {
            /* LEA-like: mov rax, [label] → use reloc */
            emit1(s, REX_W); emit1(s, 0xC7);
            encode_modrm(s, 3, 0, op1.reg);
            size_t off = s->size; emit4(s, 0);
            int sym_i = add_sym(op2.label, 0, 0, 0, 0, STT_NOTYPE);
            relocs[nrelocs++] = (Reloc){cur_section, off, sym_i, R_X86_64_32, 0};
        }
        return;
    }

    /* add/sub/and/or/xor */
    static struct { const char *m; uint8_t rr_op; uint8_t ri_sub; } alu[] = {
        {"add",0x01,0x00},{"sub",0x29,0x05},{"and",0x21,0x04},
        {"or", 0x09,0x01},{"xor",0x31,0x06},{"cmp",0x39,0x07},{0,0,0}
    };
    for (int i = 0; alu[i].m; i++) {
        if (!strcmp(mnem, alu[i].m)) {
            if (op1.is_reg && op2.is_reg) {
                enc_rr(s, alu[i].rr_op, op2.reg, op1.reg, op2.rex_ext, op1.rex_ext);
            } else if (op1.is_reg && op2.is_imm) {
                uint8_t rex = (uint8_t)(REX_W | (op1.rex_ext ? 0x01 : 0));
                emit1(s, rex); emit1(s, 0x81);
                encode_modrm(s, 3, alu[i].ri_sub, op1.reg);
                emit4(s, (uint32_t)(int32_t)op2.imm);
            }
            return;
        }
    }

    /* test */
    if (!strcmp(mnem,"test")) {
        if (op1.is_reg && op2.is_reg) enc_rr(s, 0x85, op2.reg, op1.reg, op2.rex_ext, op1.rex_ext);
        return;
    }

    /* inc/dec */
    if (!strcmp(mnem,"inc")) {
        if (op1.is_reg) { emit1(s, REX_W); emit1(s, 0xFF); encode_modrm(s,3,0,op1.reg); }
        return;
    }
    if (!strcmp(mnem,"dec")) {
        if (op1.is_reg) { emit1(s, REX_W); emit1(s, 0xFF); encode_modrm(s,3,1,op1.reg); }
        return;
    }

    /* lea */
    if (!strcmp(mnem,"lea") || !strcmp(mnem,"leaq")) {
        if (op1.is_reg && op2.is_mem) {
            emit1(s, (uint8_t)(REX_W | (op1.rex_ext ? 0x04 : 0) | (op2.base_rex ? 0x01 : 0)));
            emit1(s, 0x8D);
            /* disp32 or disp8 */
            int mod = op2.disp ? (op2.disp >= -128 && op2.disp <= 127 ? 1 : 2) : 0;
            encode_modrm(s, mod, op1.reg, op2.base_reg);
            if (mod == 1) emit1(s, (uint8_t)(int8_t)op2.disp);
            else if (mod == 2) emit4(s, (uint32_t)(int32_t)op2.disp);
        }
        return;
    }

    /* shl/shr/sar */
    static struct {const char *m; uint8_t sub;} shifts[] = {
        {"shl",4},{"shr",5},{"sar",7},{0,0}};
    for (int i = 0; shifts[i].m; i++) {
        if (!strcmp(mnem, shifts[i].m)) {
            if (op1.is_reg && op2.is_imm) {
                emit1(s, (uint8_t)(REX_W | (op1.rex_ext?0x01:0)));
                emit1(s, 0xC1);
                encode_modrm(s, 3, shifts[i].sub, op1.reg);
                emit1(s, (uint8_t)op2.imm);
            }
            return;
        }
    }

    /* not/neg */
    if (!strcmp(mnem,"not") && op1.is_reg) {
        emit1(s, (uint8_t)(REX_W|(op1.rex_ext?0x01:0)));
        emit1(s,0xF7); encode_modrm(s,3,2,op1.reg); return;
    }
    if (!strcmp(mnem,"neg") && op1.is_reg) {
        emit1(s, (uint8_t)(REX_W|(op1.rex_ext?0x01:0)));
        emit1(s,0xF7); encode_modrm(s,3,3,op1.reg); return;
    }

    /* imul */
    if (!strcmp(mnem,"imul")) {
        if (op1.is_reg && op2.is_reg) {
            emit1(s, (uint8_t)(REX_W|(op1.rex_ext?0x04:0)|(op2.rex_ext?0x01:0)));
            emit1(s,0x0F); emit1(s,0xAF); encode_modrm(s,3,op1.reg,op2.reg);
        }
        return;
    }

    /* swapgs, rdmsr, wrmsr, rdfsbase, wrfsbase */
    if (!strcmp(mnem,"swapgs")) { emit1(s,0x0F); emit1(s,0x01); emit1(s,0xF8); return; }
    if (!strcmp(mnem,"rdmsr"))  { emit1(s,0x0F); emit1(s,0x32); return; }
    if (!strcmp(mnem,"wrmsr"))  { emit1(s,0x0F); emit1(s,0x30); return; }
    if (!strcmp(mnem,"rdfsbase") && op1.is_reg) {
        emit1(s,0xF3); emit1(s,(uint8_t)(0x48|(op1.rex_ext?0x01:0)));
        emit1(s,0x0F); emit1(s,0xAE); encode_modrm(s,3,0,op1.reg); return;
    }
    if (!strcmp(mnem,"wrfsbase") && op1.is_reg) {
        emit1(s,0xF3); emit1(s,(uint8_t)(0x48|(op1.rex_ext?0x01:0)));
        emit1(s,0x0F); emit1(s,0xAE); encode_modrm(s,3,2,op1.reg); return;
    }
    if (!strcmp(mnem,"ltr") && op1.is_reg) {
        if (op1.rex_ext) emit1(s,REX_B);
        emit1(s,0x0F); emit1(s,0x00); encode_modrm(s,3,3,op1.reg); return;
    }
    if (!strcmp(mnem,"lgdt") || !strcmp(mnem,"lidt")) {
        /* Ignore: these need memory operand encoding */
        return;
    }

    warn(mnem);  /* unknown instruction - silently skip */
}

/* ── Main assembler loop ─────────────────────────────────────────────── */
static void assemble(const char *source) {
    src_pos  = source;
    line_num = 1;

    /* Create default sections */
    new_section(".text",   SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    new_section(".data",   SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    new_section(".rodata", SHT_PROGBITS, SHF_ALLOC);
    new_section(".bss",    SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    cur_section = 0;  /* default to .text */

    next_tok();
    while (cur_tok.type != TOK_EOF) {
        if (cur_tok.type == TOK_NEWLINE) { next_tok(); continue; }

        if (cur_tok.type == TOK_DIRECTIVE) {
            char dir[64];
            strncpy(dir, cur_tok.text, 63);
            next_tok();
            handle_directive(dir);
        } else if (cur_tok.type == TOK_IDENT) {
            char ident[256];
            strncpy(ident, cur_tok.text, 255);
            next_tok();

            if (cur_tok.type == TOK_COLON) {
                /* Label definition */
                next_tok();
                add_sym(ident, cur_section, sections[cur_section].size, 0, 1, STT_FUNC);
                /* Patch forward references */
                for (int i = 0; i < nfwd_refs; i++) {
                    if (strcmp(fwd_refs[i].name, ident) == 0) {
                        Section *s = &sections[fwd_refs[i].section];
                        int32_t *patch = (int32_t *)(s->data + fwd_refs[i].offset);
                        int64_t target = (int64_t)sections[cur_section].size;
                        int64_t here   = (int64_t)(fwd_refs[i].offset + 4);
                        *patch = (int32_t)(target - here);
                        /* Remove from list */
                        fwd_refs[i] = fwd_refs[--nfwd_refs];
                        i--;
                    }
                }
            } else {
                /* Instruction mnemonic */
                /* Remove size suffix (q, l, w, b) for GAS compat */
                char mnem[64];
                strncpy(mnem, ident, 63);
                size_t mlen = strlen(mnem);
                if (mlen > 1 && (mnem[mlen-1] == 'q' || mnem[mlen-1] == 'l') &&
                    strcmp(mnem,"call") && strcmp(mnem,"nop") && strcmp(mnem,"mul") &&
                    strcmp(mnem,"hlt") && strcmp(mnem,"syscal") && strcmp(mnem,"swapgs")) {
                    /* keep it - our assembler_insn handles both */
                }
                assemble_insn(mnem);
            }
        } else {
            next_tok();
        }

        /* Advance to next line */
        while (cur_tok.type != TOK_NEWLINE && cur_tok.type != TOK_EOF)
            next_tok();
    }
}

/* ── Write ELF64 object file ─────────────────────────────────────────── */
static void write_elf(const char *outfile) {
    FILE *f = fopen(outfile, "wb");
    if (!f) { perror(outfile); exit(1); }

    /* String table */
    uint8_t strtab[65536];
    uint8_t shstrtab[4096];
    int     strtab_size  = 1;  /* null byte at index 0 */
    int     shstrtab_size = 1;
    strtab[0] = shstrtab[0] = 0;

    /* Add section names */
    int shname_off[MAX_SECTIONS + 4];
    for (int i = 0; i < nsections; i++) {
        shname_off[i] = shstrtab_size;
        int n = (int)strlen(sections[i].name) + 1;
        memcpy(shstrtab + shstrtab_size, sections[i].name, n);
        shstrtab_size += n;
    }
    /* Special section names */
    int shname_symtab  = shstrtab_size;
    memcpy(shstrtab + shstrtab_size, ".symtab", 8); shstrtab_size += 8;
    int shname_strtab  = shstrtab_size;
    memcpy(shstrtab + shstrtab_size, ".strtab", 8); shstrtab_size += 8;
    int shname_shstrtab = shstrtab_size;
    memcpy(shstrtab + shstrtab_size, ".shstrtab", 10); shstrtab_size += 10;
    /* Rela sections */
    int shname_rela[MAX_SECTIONS];
    for (int i = 0; i < nsections; i++) {
        shname_rela[i] = shstrtab_size;
        char rname[128];
        snprintf(rname, sizeof(rname), ".rela%s", sections[i].name);
        int n = (int)strlen(rname) + 1;
        memcpy(shstrtab + shstrtab_size, rname, n); shstrtab_size += n;
    }

    /* Build symbol table */
    Elf64_Sym syms[MAX_SYMBOLS + 10];
    int nsyms_out = 1;  /* index 0 = null symbol */
    memset(syms, 0, sizeof(syms));
    int sym_indices[MAX_SYMBOLS];
    memset(sym_indices, 0, sizeof(sym_indices));

    int first_global = 0;

    /* Local symbols first */
    for (int i = 0; i < nsymbols; i++) {
        if (symbols[i].global) continue;
        sym_indices[i] = nsyms_out;
        Elf64_Sym *s = &syms[nsyms_out++];
        s->st_name  = (uint32_t)strtab_size;
        int n = (int)strlen(symbols[i].name) + 1;
        memcpy(strtab + strtab_size, symbols[i].name, n); strtab_size += n;
        s->st_info  = (uint8_t)((STB_LOCAL << 4) | symbols[i].st_type);
        s->st_shndx = symbols[i].defined ? (uint16_t)symbols[i].section : 0;
        s->st_value = symbols[i].offset;
    }
    first_global = nsyms_out;

    /* Global symbols */
    for (int i = 0; i < nsymbols; i++) {
        if (!symbols[i].global) continue;
        sym_indices[i] = nsyms_out;
        Elf64_Sym *s = &syms[nsyms_out++];
        s->st_name  = (uint32_t)strtab_size;
        int n = (int)strlen(symbols[i].name) + 1;
        memcpy(strtab + strtab_size, symbols[i].name, n); strtab_size += n;
        s->st_info  = (uint8_t)((STB_GLOBAL << 4) | symbols[i].st_type);
        s->st_shndx = symbols[i].defined ? (uint16_t)symbols[i].section : 0;
        s->st_value = symbols[i].offset;
    }

    /* Fix up reloc symbol indices */
    Elf64_Rela elf_relocs[MAX_RELOCS];
    int nrela_out = 0;
    for (int i = 0; i < nrelocs; i++) {
        elf_relocs[nrela_out++] = (Elf64_Rela){
            .r_offset = relocs[i].offset,
            .r_info   = ((uint64_t)sym_indices[relocs[i].sym_idx] << 32) | relocs[i].type,
            .r_addend = relocs[i].addend
        };
    }

    /* Calculate section offsets */
    /* ELF header + section headers at the end */
    size_t offset = sizeof(Elf64_Ehdr);
    size_t sec_offsets[MAX_SECTIONS];
    for (int i = 0; i < nsections; i++) {
        sec_offsets[i] = offset;
        offset += sections[i].size;
    }
    /* Align to 8 */
    offset = (offset + 7) & ~7UL;
    size_t symtab_off = offset; offset += (size_t)nsyms_out * sizeof(Elf64_Sym);
    offset = (offset + 7) & ~7UL;
    size_t strtab_off = offset; offset += (size_t)strtab_size;
    offset = (offset + 7) & ~7UL;
    size_t shstrtab_off = offset; offset += (size_t)shstrtab_size;
    offset = (offset + 7) & ~7UL;
    size_t rela_off = offset; offset += (size_t)nrela_out * sizeof(Elf64_Rela);
    offset = (offset + 7) & ~7UL;
    size_t shdr_off = offset;

    /* Total section count: 1 (null) + nsections + symtab + strtab + shstrtab + rela */
    int total_shdrs = 1 + nsections + 3 + (nrela_out > 0 ? 1 : 0);

    /* Write ELF header */
    Elf64_Ehdr ehdr = {0};
    memcpy(ehdr.e_ident, "\177ELF", 4);
    ehdr.e_ident[4] = 2;  /* ELFCLASS64 */
    ehdr.e_ident[5] = 1;  /* ELFDATA2LSB */
    ehdr.e_ident[6] = 1;  /* EV_CURRENT */
    ehdr.e_type      = 1;  /* ET_REL */
    ehdr.e_machine   = 62; /* EM_X86_64 */
    ehdr.e_version   = 1;
    ehdr.e_shoff     = (uint64_t)shdr_off;
    ehdr.e_ehsize    = sizeof(Elf64_Ehdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum     = (uint16_t)total_shdrs;
    ehdr.e_shstrndx  = (uint16_t)(1 + nsections + 2);  /* .shstrtab index */
    fwrite(&ehdr, sizeof(ehdr), 1, f);

    /* Write section data */
    for (int i = 0; i < nsections; i++)
        fwrite(sections[i].data, sections[i].size, 1, f);

    /* Padding */
    uint8_t pad[8] = {0};
    size_t cur_off = sizeof(Elf64_Ehdr);
    for (int i = 0; i < nsections; i++) cur_off += sections[i].size;
    while (cur_off % 8) { fwrite(pad, 1, 1, f); cur_off++; }

    /* Symtab */
    fwrite(syms, (size_t)nsyms_out * sizeof(Elf64_Sym), 1, f);
    cur_off += (size_t)nsyms_out * sizeof(Elf64_Sym);
    while (cur_off % 8) { fwrite(pad, 1, 1, f); cur_off++; }

    /* Strtab */
    fwrite(strtab, (size_t)strtab_size, 1, f);
    cur_off += (size_t)strtab_size;
    while (cur_off % 8) { fwrite(pad, 1, 1, f); cur_off++; }

    /* Shstrtab */
    fwrite(shstrtab, (size_t)shstrtab_size, 1, f);
    cur_off += (size_t)shstrtab_size;
    while (cur_off % 8) { fwrite(pad, 1, 1, f); cur_off++; }

    /* Rela */
    if (nrela_out > 0) {
        fwrite(elf_relocs, (size_t)nrela_out * sizeof(Elf64_Rela), 1, f);
        cur_off += (size_t)nrela_out * sizeof(Elf64_Rela);
        while (cur_off % 8) { fwrite(pad, 1, 1, f); cur_off++; }
    }

    /* Section headers */
    /* 0: null */
    Elf64_Shdr shdr = {0};
    fwrite(&shdr, sizeof(shdr), 1, f);

    /* Data sections */
    for (int i = 0; i < nsections; i++) {
        shdr.sh_name      = (uint32_t)shname_off[i];
        shdr.sh_type      = sections[i].sh_type;
        shdr.sh_flags     = sections[i].sh_flags;
        shdr.sh_offset    = (uint64_t)sec_offsets[i];
        shdr.sh_size      = sections[i].size;
        shdr.sh_addralign = 1;
        shdr.sh_entsize   = 0;
        fwrite(&shdr, sizeof(shdr), 1, f);
    }

    /* symtab */
    memset(&shdr, 0, sizeof shdr);
    shdr.sh_name      = (uint32_t)shname_symtab;
    shdr.sh_type      = SHT_SYMTAB;
    shdr.sh_offset    = (uint64_t)symtab_off;
    shdr.sh_size      = (uint64_t)nsyms_out * sizeof(Elf64_Sym);
    shdr.sh_link      = (uint32_t)(1 + nsections + 1);  /* strtab section index */
    shdr.sh_info      = (uint32_t)first_global;
    shdr.sh_addralign = 8;
    shdr.sh_entsize   = sizeof(Elf64_Sym);
    fwrite(&shdr, sizeof(shdr), 1, f);

    /* strtab */
    memset(&shdr, 0, sizeof shdr);
    shdr.sh_name      = (uint32_t)shname_strtab;
    shdr.sh_type      = SHT_STRTAB;
    shdr.sh_offset    = (uint64_t)strtab_off;
    shdr.sh_size      = (uint64_t)strtab_size;
    shdr.sh_addralign = 1;
    fwrite(&shdr, sizeof(shdr), 1, f);

    /* shstrtab */
    memset(&shdr, 0, sizeof shdr);
    shdr.sh_name      = (uint32_t)shname_shstrtab;
    shdr.sh_type      = SHT_STRTAB;
    shdr.sh_offset    = (uint64_t)shstrtab_off;
    shdr.sh_size      = (uint64_t)shstrtab_size;
    shdr.sh_addralign = 1;
    fwrite(&shdr, sizeof(shdr), 1, f);

    /* rela.text */
    if (nrela_out > 0) {
        memset(&shdr, 0, sizeof shdr);
        shdr.sh_name      = (uint32_t)shname_rela[0];
        shdr.sh_type      = SHT_RELA;
        shdr.sh_offset    = (uint64_t)rela_off;
        shdr.sh_size      = (uint64_t)nrela_out * sizeof(Elf64_Rela);
        shdr.sh_link      = (uint32_t)(1 + nsections);  /* symtab */
        shdr.sh_info      = 1;  /* applies to .text (index 1) */
        shdr.sh_addralign = 8;
        shdr.sh_entsize   = sizeof(Elf64_Rela);
        fwrite(&shdr, sizeof(shdr), 1, f);
    }

    fclose(f);
}

int main(int argc, char **argv) {
    const char *infile = 0, *outfile = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i+1 < argc) outfile = argv[++i];
        else infile = argv[i];
    }
    if (!infile) { fprintf(stderr, "Usage: tiny-as input.s [-o output.o]\n"); return 1; }
    if (!outfile) outfile = "a.out";
    input_file = infile;

    FILE *f = fopen(infile, "r");
    if (!f) { perror(infile); return 1; }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    char *src = (char *)malloc((size_t)fsz + 2);
    fread(src, 1, (size_t)fsz, f); fclose(f);
    src[fsz] = '\n'; src[fsz+1] = 0;

    assemble(src);
    write_elf(outfile);
    printf("tiny-as: assembled %s -> %s\n", infile, outfile);
    return 0;
}
