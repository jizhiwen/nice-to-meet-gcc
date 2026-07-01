/* string.c - String and memory functions for libc */
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n) {
    char *d = dst; const char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}
void *memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}
void *memmove(void *dst, const void *src, size_t n) {
    char *d = dst; const char *s = src;
    if (d < s) for (size_t i = 0; i < n; i++) d[i] = s[i];
    else       for (size_t i = n; i > 0; i--) d[i-1] = s[i-1];
    return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}
void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    for (size_t i = 0; i < n; i++) if (p[i] == (unsigned char)c) return (void *)(p+i);
    return 0;
}

size_t strlen(const char *s) { size_t n=0; while(s[n]) n++; return n; }
size_t strnlen(const char *s, size_t m) { size_t n=0; while(n<m && s[n]) n++; return n; }

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i=0; i<n && (a[i]||b[i]); i++)
        if (a[i]!=b[i]) return (unsigned char)a[i]-(unsigned char)b[i];
    return 0;
}
int strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = *a >= 'A' && *a <= 'Z' ? *a+32 : *a;
        int cb = *b >= 'A' && *b <= 'Z' ? *b+32 : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
char *strcpy(char *dst, const char *src) {
    char *r=dst; while((*dst++=*src++)); return r;
}
char *strncpy(char *dst, const char *src, size_t n) {
    size_t i=0; for(;i<n&&src[i];i++) dst[i]=src[i]; for(;i<n;i++) dst[i]=0;
    return dst;
}
char *strcat(char *dst, const char *src) {
    char *r=dst; while(*dst) dst++; while((*dst++=*src++)); return r;
}
char *strncat(char *dst, const char *src, size_t n) {
    char *r=dst; while(*dst) dst++;
    for(size_t i=0; i<n && src[i]; i++) *dst++=src[i];
    *dst=0; return r;
}
char *strchr(const char *s, int c) {
    for(;*s;s++) if((unsigned char)*s==(unsigned char)c) return (char*)s;
    return (unsigned char)c==0?(char*)s:0;
}
char *strrchr(const char *s, int c) {
    const char *r=0;
    for(;*s;s++) if((unsigned char)*s==(unsigned char)c) r=s;
    return (char*)r;
}
char *strstr(const char *h, const char *n) {
    if(!*n) return (char*)h;
    for(;*h;h++) {
        const char *a=h,*b=n;
        while(*a&&*b&&*a==*b){a++;b++;}
        if(!*b) return (char*)h;
    }
    return 0;
}
static char *strtok_save = 0;
char *strtok(char *s, const char *delim) { return strtok_r(s, delim, &strtok_save); }
char *strtok_r(char *s, const char *delim, char **save) {
    if(s) *save=s;
    if(!*save) return 0;
    /* skip leading delimiters */
    while(**save && strchr(delim,**save)) (*save)++;
    if(!**save) return 0;
    char *tok=*save;
    while(**save && !strchr(delim,**save)) (*save)++;
    if(**save) { **save=0; (*save)++; }
    return tok;
}
char *strdup(const char *s) {
    size_t n=strlen(s)+1; char *p=malloc(n); if(p) memcpy(p,s,n); return p;
}
char *strndup(const char *s, size_t n) {
    size_t len=strnlen(s,n); char *p=malloc(len+1);
    if(p){memcpy(p,s,len);p[len]=0;} return p;
}
char *strerror(int e) {
    static char buf[32];
    switch(e){
    case 1: return "Operation not permitted";
    case 2: return "No such file or directory";
    case 9: return "Bad file descriptor";
    case 11: return "Resource temporarily unavailable";
    case 12: return "Out of memory";
    case 13: return "Permission denied";
    case 14: return "Bad address";
    case 22: return "Invalid argument";
    case 38: return "Function not implemented";
    default:
        snprintf(buf,sizeof(buf),"Error %d",e);
        return buf;
    }
}
