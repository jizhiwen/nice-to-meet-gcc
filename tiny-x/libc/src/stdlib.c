/*
 * stdlib.c - Standard library: malloc, exit, atoi, qsort, etc.
 *
 * malloc: uses a simple free-list allocator over mmap'd pages.
 * Each block has a 16-byte header: [size][flags][next_free]
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

int errno = 0;

/* ── malloc ─────────────────────────────────────────────────────────── */
#define HEAP_CHUNK  (64 * 1024)    /* 64KB per chunk from mmap */
#define ALIGN_SIZE  16

typedef struct block_hdr {
    size_t          size;          /* payload size */
    int             free;
    struct block_hdr *next;
} block_hdr_t;

static block_hdr_t *free_list = 0;

static block_hdr_t *request_memory(size_t size)
{
    size_t total = size + sizeof(block_hdr_t);
    if (total < HEAP_CHUNK) total = HEAP_CHUNK;
    void *p = mmap(0, total, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) return 0;
    block_hdr_t *b = (block_hdr_t *)p;
    b->size = total - sizeof(block_hdr_t);
    b->free = 1;
    b->next = 0;
    return b;
}

void *malloc(size_t size)
{
    if (!size) return 0;
    /* Align to 16 bytes */
    size = (size + ALIGN_SIZE - 1) & ~(ALIGN_SIZE - 1);

    /* Walk free list */
    block_hdr_t *best = 0, *best_prev = 0;
    block_hdr_t *b = free_list, *prev = 0;
    while (b) {
        if (b->free && b->size >= size) {
            if (!best || b->size < best->size) {
                best = b; best_prev = prev;
            }
        }
        prev = b; b = b->next;
    }

    if (!best) {
        /* No suitable block, request more */
        block_hdr_t *nb = request_memory(size);
        if (!nb) { errno = ENOMEM; return 0; }
        /* Add to free list */
        nb->next = free_list;
        free_list = nb;
        best = nb; best_prev = 0;
    }

    /* Split block if large enough */
    if (best->size >= size + sizeof(block_hdr_t) + ALIGN_SIZE) {
        block_hdr_t *split = (block_hdr_t *)((char *)best + sizeof(block_hdr_t) + size);
        split->size = best->size - size - sizeof(block_hdr_t);
        split->free = 1;
        split->next = best->next;
        best->next  = split;
        best->size  = size;
    }

    best->free = 0;
    return (char *)best + sizeof(block_hdr_t);
}

void free(void *ptr)
{
    if (!ptr) return;
    block_hdr_t *b = (block_hdr_t *)((char *)ptr - sizeof(block_hdr_t));
    b->free = 1;

    /* Coalesce adjacent free blocks */
    block_hdr_t *cur = free_list;
    while (cur) {
        if (cur->free && cur->next && cur->next->free) {
            cur->size += sizeof(block_hdr_t) + cur->next->size;
            cur->next  = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr) return malloc(size);
    if (!size) { free(ptr); return 0; }
    block_hdr_t *b = (block_hdr_t *)((char *)ptr - sizeof(block_hdr_t));
    if (b->size >= size) return ptr;
    void *np = malloc(size);
    if (!np) return 0;
    memcpy(np, ptr, b->size);
    free(ptr);
    return np;
}

/* ── exit ────────────────────────────────────────────────────────────── */
typedef void (*atexit_fn)(void);
static atexit_fn atexit_funcs[32];
static int       atexit_count = 0;

int atexit(atexit_fn fn)
{
    if (atexit_count < 32) { atexit_funcs[atexit_count++] = fn; return 0; }
    return -1;
}

void exit(int status)
{
    for (int i = atexit_count - 1; i >= 0; i--) atexit_funcs[i]();
    _exit(status);
}

void abort(void)
{
    _exit(134);
}

/* ── String conversion ───────────────────────────────────────────────── */
int atoi(const char *s)
{
    return (int)strtol(s, 0, 10);
}

long atol(const char *s)
{
    return strtol(s, 0, 10);
}

long long atoll(const char *s)
{
    return (long long)strtoul(s, 0, 10);
}

long strtol(const char *s, char **end, int base)
{
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    if (base == 0) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (*s == '0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    long v = 0;
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        s++;
    }
    if (end) *end = (char *)s;
    return neg ? -v : v;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    return (unsigned long)strtol(s, end, base);
}

double strtod(const char *s, char **end)
{
    /* Minimal: just handle integers */
    long v = strtol(s, end, 10);
    return (double)v;
}

int abs(int n) { return n < 0 ? -n : n; }
long labs(long n) { return n < 0 ? -n : n; }

/* ── Environment ─────────────────────────────────────────────────────── */
/* Passed via execve; we keep a local copy */
static char **_environ = 0;

void __libc_start_env(char **envp)
{
    _environ = envp;
}

char *getenv(const char *name)
{
    if (!_environ) return 0;
    size_t len = strlen(name);
    for (char **e = _environ; *e; e++) {
        if (strncmp(*e, name, len) == 0 && (*e)[len] == '=')
            return *e + len + 1;
    }
    return 0;
}

int setenv(const char *name, const char *value, int overwrite)
{
    (void)name; (void)value; (void)overwrite;
    return -1;  /* not implemented */
}

/* ── qsort ───────────────────────────────────────────────────────────── */
static void swap(char *a, char *b, size_t size)
{
    char tmp;
    for (size_t i = 0; i < size; i++) {
        tmp = a[i]; a[i] = b[i]; b[i] = tmp;
    }
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *))
{
    if (nmemb <= 1) return;
    char *arr = (char *)base;
    /* Simple insertion sort for small arrays, quicksort for larger */
    if (nmemb < 16) {
        for (size_t i = 1; i < nmemb; i++) {
            for (size_t j = i; j > 0 && compar(arr+(j-1)*size, arr+j*size) > 0; j--)
                swap(arr+(j-1)*size, arr+j*size, size);
        }
        return;
    }
    /* Quicksort: median-of-3 pivot */
    size_t lo = 0, hi = nmemb - 1, mid = nmemb / 2;
    if (compar(arr+lo*size, arr+mid*size) > 0) swap(arr+lo*size, arr+mid*size, size);
    if (compar(arr+lo*size, arr+hi*size)  > 0) swap(arr+lo*size, arr+hi*size, size);
    if (compar(arr+mid*size, arr+hi*size) > 0) swap(arr+mid*size, arr+hi*size, size);
    char *pivot = arr + mid * size;

    size_t i = lo + 1, j = hi - 1;
    for (;;) {
        while (i <= j && compar(arr+i*size, pivot) < 0) i++;
        while (j >= i && compar(arr+j*size, pivot) > 0) j--;
        if (i >= j) break;
        swap(arr+i*size, arr+j*size, size);
        i++; if (j > 0) j--;
    }
    swap(arr+i*size, arr+hi*size, size);
    if (i > 0) qsort(base, i, size, compar);
    if (i + 1 < nmemb) qsort(arr+(i+1)*size, nmemb-i-1, size, compar);
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *))
{
    const char *arr = (const char *)base;
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = compar(key, arr + mid * size);
        if (c == 0) return (void *)(arr + mid * size);
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    return 0;
}

int system(const char *cmd)
{
    (void)cmd;
    return -1;  /* not implemented */
}
