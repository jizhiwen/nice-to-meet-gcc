#ifndef UNISTD_H
#define UNISTD_H

#include <stddef.h>
#include <sys/types.h>

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int     open(const char *path, int flags, ...);
int     close(int fd);
pid_t   fork(void);
int     execve(const char *path, char *const argv[], char *const envp[]);
int     execvp(const char *file, char *const argv[]);
int     execv(const char *path, char *const argv[]);
void    _exit(int status) __attribute__((noreturn));
pid_t   getpid(void);
pid_t   getppid(void);
int     getcwd_raw(char *buf, size_t size);
int     chdir(const char *path);
ssize_t readlink(const char *path, char *buf, size_t bufsiz);
int     access(const char *path, int mode);
int     isatty(int fd);
off_t   lseek(int fd, off_t offset, int whence);
int     dup(int fd);
int     dup2(int fd1, int fd2);
int     pipe(int pipefd[2]);
int     unlink(const char *path);
unsigned sleep(unsigned seconds);

/* Open flags */
#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     0100
#define O_TRUNC     01000
#define O_APPEND    02000
#define O_NONBLOCK  04000
#define O_CLOEXEC   02000000

/* Access modes */
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

char   *getcwd(char *buf, size_t size);
int     dup2(int oldfd, int newfd);
int     pipe(int pipefd[2]);
pid_t   wait4(pid_t pid, int *status, int options, void *rusage);

#endif /* UNISTD_H */
