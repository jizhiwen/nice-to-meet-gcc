/*
 * stdio.c - Standard I/O library
 *
 * Implements printf, puts, getchar, fopen/fclose/fread/fwrite, etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>

/* ── File streams ────────────────────────────────────────────────────── */
#define FILE_BUFSIZE 4096

static FILE _stdin_file  = { .fd = 0, .flags = 1 };
static FILE _stdout_file = { .fd = 1, .flags = 2 };
static FILE _stderr_file = { .fd = 2, .flags = 2 };

FILE *stdin  = &_stdin_file;
FILE *stdout = &_stdout_file;
FILE *stderr = &_stderr_file;

/* ── vsnprintf ───────────────────────────────────────────────────────── */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    if (!buf || !size) return 0;
    size_t pos = 0;

    #define EMIT(c) do { if (pos < size - 1) buf[pos++] = (c); } while(0)

    while (*fmt) {
        if (*fmt != '%') { EMIT(*fmt++); continue; }
        fmt++;

        /* Flags */
        int flag_minus = 0, flag_zero = 0, flag_plus = 0;
        while (*fmt == '-' || *fmt == '0' || *fmt == '+') {
            if (*fmt == '-') flag_minus = 1;
            if (*fmt == '0') flag_zero  = 1;
            if (*fmt == '+') flag_plus  = 1;
            fmt++;
        }

        /* Width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');

        /* Precision */
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
        }

        /* Length modifier */
        int is_long = 0, is_longlong = 0, is_size = 0;
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') { fmt++; is_longlong = 1; }
            else is_long = 1;
        } else if (*fmt == 'z') { fmt++; is_size = 1; }
        else if (*fmt == 'h') { fmt++; if (*fmt == 'h') fmt++; }

        char tmp[64];
        const char *s = tmp;
        int slen = 0;

        switch (*fmt) {
        case 'd': case 'i': {
            long long v;
            if (is_longlong) v = va_arg(ap, long long);
            else if (is_long || is_size) v = va_arg(ap, long);
            else v = (int)va_arg(ap, int);
            int neg = (v < 0);
            unsigned long long uv = neg ? (unsigned long long)(-v) : (unsigned long long)v;
            int i = sizeof(tmp) - 1;
            tmp[i--] = '\0';
            if (!uv) tmp[i--] = '0';
            while (uv) { tmp[i--] = '0' + (int)(uv % 10); uv /= 10; }
            if (neg) tmp[i--] = '-';
            else if (flag_plus) tmp[i--] = '+';
            s = &tmp[i+1]; slen = (int)(sizeof(tmp) - 1 - i - 1);
            break;
        }
        case 'u': {
            unsigned long long v;
            if (is_longlong) v = va_arg(ap, unsigned long long);
            else if (is_long || is_size) v = va_arg(ap, unsigned long);
            else v = va_arg(ap, unsigned int);
            int i = sizeof(tmp) - 1; tmp[i--] = '\0';
            if (!v) tmp[i--] = '0';
            while (v) { tmp[i--] = '0' + (int)(v % 10); v /= 10; }
            s = &tmp[i+1]; slen = (int)(sizeof(tmp) - 1 - i - 1);
            break;
        }
        case 'x': case 'X': {
            unsigned long long v;
            if (is_longlong) v = va_arg(ap, unsigned long long);
            else if (is_long || is_size) v = va_arg(ap, unsigned long);
            else v = va_arg(ap, unsigned int);
            const char *hexc = (*fmt == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
            int i = sizeof(tmp) - 1; tmp[i--] = '\0';
            if (!v) tmp[i--] = '0';
            while (v) { tmp[i--] = hexc[v & 0xF]; v >>= 4; }
            s = &tmp[i+1]; slen = (int)(sizeof(tmp) - 1 - i - 1);
            break;
        }
        case 'p': {
            unsigned long long v = (unsigned long long)(uintptr_t)va_arg(ap, void *);
            EMIT('0'); EMIT('x');
            const char *hexc = "0123456789abcdef";
            int i = sizeof(tmp) - 1; tmp[i--] = '\0';
            if (!v) tmp[i--] = '0';
            while (v) { tmp[i--] = hexc[v & 0xF]; v >>= 4; }
            s = &tmp[i+1]; slen = (int)(sizeof(tmp) - 1 - i - 1);
            width = 0;  /* already emitted "0x" */
            break;
        }
        case 's': {
            s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            slen = (int)strlen(s);
            if (prec >= 0 && slen > prec) slen = prec;
            break;
        }
        case 'c': {
            tmp[0] = (char)va_arg(ap, int);
            tmp[1] = '\0'; s = tmp; slen = 1;
            break;
        }
        case '%': EMIT('%'); fmt++; continue;
        case 'n': *(va_arg(ap, int *)) = (int)pos; fmt++; continue;
        default:  EMIT('%'); EMIT(*fmt); fmt++; continue;
        }
        fmt++;

        /* Padding */
        char pad_char = (flag_zero && !flag_minus) ? '0' : ' ';
        if (!flag_minus) {
            for (int i = slen; i < width; i++) EMIT(pad_char);
        }
        for (int i = 0; i < slen; i++) EMIT(s[i]);
        if (flag_minus) {
            for (int i = slen; i < width; i++) EMIT(' ');
        }
    }
    #undef EMIT
    buf[pos] = '\0';
    return (int)pos;
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int vfprintf(FILE *stream, const char *fmt, va_list ap)
{
    char buf[4096];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0) {
        ssize_t r = write(stream->fd, buf, (size_t)n);
        return (int)r;
    }
    return n;
}

int printf(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}

int fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int r = vfprintf(stream, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(buf, 65536, fmt, ap);
    va_end(ap);
    return r;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

/* ── Basic I/O ───────────────────────────────────────────────────────── */
int putchar(int c)
{
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
    return c;
}

int puts(const char *s)
{
    int r = (int)write(STDOUT_FILENO, s, strlen(s));
    write(STDOUT_FILENO, "\n", 1);
    return r;
}

int getchar(void)
{
    char c;
    ssize_t r = read(STDIN_FILENO, &c, 1);
    if (r <= 0) return EOF;
    return (unsigned char)c;
}

char *fgets(char *s, int n, FILE *stream)
{
    int i = 0;
    while (i < n - 1) {
        char c;
        ssize_t r = read(stream->fd, &c, 1);
        if (r <= 0) break;
        s[i++] = c;
        if (c == '\n') break;
    }
    if (i == 0) return 0;
    s[i] = '\0';
    return s;
}

/* ── File I/O ────────────────────────────────────────────────────────── */
FILE *fopen(const char *path, const char *mode)
{
    int flags = O_RDONLY;
    if (mode[0] == 'w') flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a') flags = O_WRONLY | O_CREAT | O_APPEND;
    if (mode[1] == '+' || (mode[0] != 'r' && strchr(mode, 'r'))) flags = O_RDWR;

    int fd = open(path, flags, 0644);
    if (fd < 0) return 0;

    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) { close(fd); return 0; }
    f->fd      = fd;
    f->flags   = (mode[0] == 'r') ? 1 : 2;
    f->buf_pos = 0;
    f->buf_len = 0;
    f->buf     = 0;
    f->eof     = 0;
    f->error   = 0;
    return f;
}

int fclose(FILE *stream)
{
    if (!stream || stream == stdin || stream == stdout || stream == stderr) return EOF;
    close(stream->fd);
    if (stream->buf) free(stream->buf);
    free(stream);
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    ssize_t r = read(stream->fd, ptr, size * nmemb);
    if (r <= 0) { stream->eof = 1; return 0; }
    return (size_t)r / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    ssize_t r = write(stream->fd, ptr, size * nmemb);
    if (r <= 0) { stream->error = 1; return 0; }
    return (size_t)r / size;
}

int fputc(int c, FILE *stream)
{
    char ch = (char)c;
    ssize_t r = write(stream->fd, &ch, 1);
    return (r == 1) ? (unsigned char)c : EOF;
}

int fputs(const char *s, FILE *stream)
{
    return (int)write(stream->fd, s, strlen(s));
}

int fflush(FILE *stream)
{
    (void)stream;
    return 0;  /* unbuffered in this implementation */
}

long ftell(FILE *stream)
{
    return (long)lseek(stream->fd, 0, SEEK_CUR);
}

int fseek(FILE *stream, long offset, int whence)
{
    return (lseek(stream->fd, offset, whence) < 0) ? -1 : 0;
}

int feof(FILE *stream)   { return stream->eof; }
int ferror(FILE *stream) { return stream->error; }

void perror(const char *msg)
{
    extern int errno;
    if (msg && msg[0]) {
        write(2, msg, strlen(msg));
        write(2, ": ", 2);
    }
    char *e = strerror(errno);
    write(2, e, strlen(e));
    write(2, "\n", 1);
}
