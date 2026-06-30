#include "libc_tiny.h"

#define SYS_WRITE 1
#define SYS_MMAP 9
#define SYS_EXIT 60

#define PROT_READ 1
#define PROT_WRITE 2

#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20

static unsigned char* g_heap = 0;
static unsigned long g_remaining = 0;

long tiny_syscall0(long n) {
  long ret;
  __asm__ volatile("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
  return ret;
}

long tiny_syscall1(long n, long a1) {
  long ret;
  __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
  return ret;
}

long tiny_syscall2(long n, long a1, long a2) {
  long ret;
  __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
  return ret;
}

long tiny_syscall3(long n, long a1, long a2, long a3) {
  long ret;
  __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
  return ret;
}

long tiny_syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
  long ret;
  register long r10 __asm__("r10") = a4;
  register long r8 __asm__("r8") = a5;
  register long r9 __asm__("r9") = a6;
  __asm__ volatile("syscall"
                   : "=a"(ret)
                   : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                   : "rcx", "r11", "memory");
  return ret;
}

void tiny_exit(int code) {
  tiny_syscall1(SYS_EXIT, code);
  for (;;) {
  }
}

long tiny_write(int fd, const void* buf, unsigned long count) {
  return tiny_syscall3(SYS_WRITE, fd, (long)buf, (long)count);
}

unsigned long tiny_strlen(const char* s) {
  unsigned long n = 0;
  while (s[n] != '\0') {
    n++;
  }
  return n;
}

long tiny_puts(const char* s) {
  long n = (long)tiny_strlen(s);
  long w1 = tiny_write(1, s, (unsigned long)n);
  long w2 = tiny_write(1, "\n", 1);
  return (w1 < 0 || w2 < 0) ? -1 : (w1 + w2);
}

void tiny_print_int(long value) {
  char buf[32];
  unsigned long len = 0;
  unsigned long i = 0;
  unsigned long j = 0;
  long x = value;
  if (x == 0) {
    tiny_write(1, "0", 1);
    return;
  }
  if (x < 0) {
    tiny_write(1, "-", 1);
    x = -x;
  }
  while (x > 0 && len < sizeof(buf)) {
    buf[len++] = (char)('0' + (x % 10));
    x /= 10;
  }
  i = 0;
  if (len > 0) {
    for (j = len - 1; j > i; j--) {
      char t = buf[i];
      buf[i] = buf[j];
      buf[j] = t;
      i++;
    }
  }
  tiny_write(1, buf, len);
}

void* tiny_memcpy(void* dst, const void* src, unsigned long n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  unsigned long i = 0;
  while (i < n) {
    d[i] = s[i];
    i++;
  }
  return dst;
}

void* tiny_memset(void* dst, int c, unsigned long n) {
  unsigned char* d = (unsigned char*)dst;
  unsigned long i = 0;
  while (i < n) {
    d[i] = (unsigned char)c;
    i++;
  }
  return dst;
}

static void* tiny_alloc_chunk(unsigned long min_bytes) {
  unsigned long chunk = 65536;
  if (min_bytes > chunk) {
    chunk = (min_bytes + 4095UL) & ~4095UL;
  }
  long p = tiny_syscall6(SYS_MMAP, 0, (long)chunk, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p < 0) {
    return 0;
  }
  g_heap = (unsigned char*)p;
  g_remaining = chunk;
  return g_heap;
}

void* tiny_malloc(unsigned long n) {
  if (n == 0) {
    n = 1;
  }
  n = (n + 7UL) & ~7UL;
  if (g_remaining < n) {
    if (tiny_alloc_chunk(n) == 0) {
      return 0;
    }
  }
  void* p = g_heap;
  g_heap += n;
  g_remaining -= n;
  return p;
}

void tiny_free(void* p) {
  (void)p;
}

void tiny_abort(void) {
  tiny_exit(127);
}
