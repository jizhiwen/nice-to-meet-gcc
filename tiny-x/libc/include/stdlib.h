#ifndef STDLIB_H
#define STDLIB_H

#include <stddef.h>
#include <stdint.h>

void  exit(int status) __attribute__((noreturn));
void  abort(void)      __attribute__((noreturn));

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void  free(void *ptr);

int   atoi(const char *s);
long  atol(const char *s);
long long atoll(const char *s);
long  strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
double strtod(const char *s, char **end);

int   abs(int n);
long  labs(long n);

/* Environment */
char *getenv(const char *name);
int   setenv(const char *name, const char *value, int overwrite);

/* qsort / bsearch */
void  qsort(void *base, size_t nmemb, size_t size,
            int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

/* System */
int   system(const char *cmd);

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#endif /* STDLIB_H */
