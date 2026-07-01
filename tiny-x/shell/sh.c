/*
 * sh.c - tiny shell (sh)
 *
 * A minimal POSIX-like shell that supports:
 *   - Command execution (fork + execve)
 *   - Built-in commands: cd, exit, echo, pwd, export, help
 *   - Environment variables ($VAR)
 *   - Pipes (|)
 *   - I/O redirection (>, <, >>)
 *   - Command history (up arrow)
 *   - Quoted strings ("..." and '...')
 *
 * This demonstrates how a real shell uses fork/exec/wait.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_ARGS    64
#define MAX_LINE    1024
#define MAX_HISTORY 32
#define PROMPT      "tiny-x$ "

/* ── Environment ────────────────────────────────────────────────────── */
static char *environ_buf[128];
static char  env_store[128][256];
static int   env_count = 0;

static void env_set(const char *name, const char *value)
{
    for (int i = 0; i < env_count; i++) {
        size_t nlen = strlen(name);
        if (strncmp(env_store[i], name, nlen) == 0 && env_store[i][nlen] == '=') {
            snprintf(env_store[i], sizeof(env_store[i]), "%s=%s", name, value);
            return;
        }
    }
    if (env_count < 128) {
        snprintf(env_store[env_count], sizeof(env_store[env_count]), "%s=%s", name, value);
        environ_buf[env_count] = env_store[env_count];
        env_count++;
        environ_buf[env_count] = 0;
    }
}

static const char *env_get(const char *name)
{
    size_t nlen = strlen(name);
    for (int i = 0; i < env_count; i++) {
        if (strncmp(env_store[i], name, nlen) == 0 && env_store[i][nlen] == '=')
            return env_store[i] + nlen + 1;
    }
    return 0;
}

/* ── History ────────────────────────────────────────────────────────── */
static char history[MAX_HISTORY][MAX_LINE];
static int  history_count = 0;
static int  history_idx   = 0;

static void history_add(const char *line)
{
    if (!line[0]) return;
    strncpy(history[history_count % MAX_HISTORY], line, MAX_LINE - 1);
    history_count++;
    history_idx = history_count;
}

/* ── readline: read a line with basic editing ────────────────────────── */
static int readline(char *buf, int maxlen)
{
    int pos = 0;
    buf[0] = '\0';

    for (;;) {
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) return -1;

        if (c == '\n' || c == '\r') {
            putchar('\n');
            buf[pos] = '\0';
            return pos;
        }
        if (c == '\b' || c == 127) {
            if (pos > 0) {
                pos--;
                write(STDOUT_FILENO, "\b \b", 3);
            }
            continue;
        }
        if (c == '\x03') {    /* Ctrl+C */
            putchar('\n');
            buf[0] = '\0';
            return 0;
        }
        if (c == '\x04') {    /* Ctrl+D (EOF) */
            if (pos == 0) return -1;
        }
        if (c == '\x1B') {    /* ESC sequence (arrow keys) */
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;
            if (seq[0] == '[') {
                if (seq[1] == 'A') {  /* Up arrow - history */
                    if (history_idx > 0) {
                        history_idx--;
                        /* Clear current line */
                        while (pos > 0) {
                            write(STDOUT_FILENO, "\b \b", 3);
                            pos--;
                        }
                        strncpy(buf, history[history_idx % MAX_HISTORY], maxlen - 1);
                        pos = (int)strlen(buf);
                        write(STDOUT_FILENO, buf, (size_t)pos);
                    }
                } else if (seq[1] == 'B') {  /* Down arrow */
                    if (history_idx < history_count) {
                        history_idx++;
                        while (pos > 0) {
                            write(STDOUT_FILENO, "\b \b", 3);
                            pos--;
                        }
                        if (history_idx < history_count) {
                            strncpy(buf, history[history_idx % MAX_HISTORY], maxlen - 1);
                        } else {
                            buf[0] = '\0';
                        }
                        pos = (int)strlen(buf);
                        if (pos > 0) write(STDOUT_FILENO, buf, (size_t)pos);
                    }
                }
            }
            continue;
        }
        if (pos < maxlen - 1) {
            buf[pos++] = c;
            buf[pos]   = '\0';
            putchar(c);
        }
    }
}

/* ── Word splitting / tokenize ───────────────────────────────────────── */
static int tokenize(char *line, char **argv, int maxargv)
{
    int argc = 0;
    char *p = line;

    while (*p && argc < maxargv - 1) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        /* Handle quotes */
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = '\0';
        } else if (*p == '\'') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '\'') p++;
            if (*p == '\'') *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    argv[argc] = 0;
    return argc;
}

/* ── Variable expansion ───────────────────────────────────────────────── */
static char expanded_args[MAX_ARGS][256];

static const char *expand_var(const char *arg)
{
    if (arg[0] != '$') return arg;
    const char *name = arg + 1;
    if (strcmp(name, "?") == 0) return "0";
    if (strcmp(name, "$") == 0) {
        static char pidbuf[16];
        snprintf(pidbuf, sizeof(pidbuf), "%d", getpid());
        return pidbuf;
    }
    const char *v = env_get(name);
    return v ? v : "";
}

/* ── Find executable in PATH ─────────────────────────────────────────── */
static int find_exec(const char *name, char *full_path, size_t pathlen)
{
    if (name[0] == '/' || name[0] == '.') {
        strncpy(full_path, name, pathlen - 1);
        return access(full_path, X_OK) == 0 ? 1 : 0;
    }
    const char *path = env_get("PATH");
    if (!path) path = "/bin:/usr/bin";
    char search[512];
    strncpy(search, path, sizeof(search) - 1);
    char *tok = strtok(search, ":");
    while (tok) {
        snprintf(full_path, pathlen, "%s/%s", tok, name);
        if (access(full_path, X_OK) == 0) return 1;
        tok = strtok(0, ":");
    }
    return 0;
}

/* ── Execute a command ────────────────────────────────────────────────── */
static int last_exit = 0;

static int run_command(char **argv, int argc, int bg)
{
    if (!argc) return 0;

    /* Expand variables */
    for (int i = 0; i < argc; i++) {
        if (argv[i][0] == '$') {
            strncpy(expanded_args[i], expand_var(argv[i]), 255);
            argv[i] = expanded_args[i];
        }
    }

    /* ── Built-in: cd ──────────────────────────────────────────────── */
    if (strcmp(argv[0], "cd") == 0) {
        const char *dir = (argc > 1) ? argv[1] : env_get("HOME");
        if (!dir) dir = "/";
        if (chdir(dir) < 0) {
            fprintf(stderr, "cd: %s: %s\n", dir, strerror(errno));
            return 1;
        }
        return 0;
    }

    /* ── Built-in: exit ────────────────────────────────────────────── */
    if (strcmp(argv[0], "exit") == 0) {
        int code = (argc > 1) ? atoi(argv[1]) : last_exit;
        exit(code);
    }

    /* ── Built-in: echo ────────────────────────────────────────────── */
    if (strcmp(argv[0], "echo") == 0) {
        int newline = 1;
        int start = 1;
        if (argc > 1 && strcmp(argv[1], "-n") == 0) { newline = 0; start = 2; }
        for (int i = start; i < argc; i++) {
            if (i > start) putchar(' ');
            /* Handle escape sequences */
            for (const char *s = argv[i]; *s; s++) {
                if (*s == '\\' && s[1]) {
                    switch (*++s) {
                    case 'n': putchar('\n'); break;
                    case 't': putchar('\t'); break;
                    case '\\': putchar('\\'); break;
                    default: putchar('\\'); putchar(*s);
                    }
                } else {
                    putchar(*s);
                }
            }
        }
        if (newline) putchar('\n');
        return 0;
    }

    /* ── Built-in: pwd ─────────────────────────────────────────────── */
    if (strcmp(argv[0], "pwd") == 0) {
        char buf[256];
        if (getcwd(buf, sizeof(buf))) puts(buf);
        return 0;
    }

    /* ── Built-in: export ──────────────────────────────────────────── */
    if (strcmp(argv[0], "export") == 0) {
        for (int i = 1; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            if (eq) {
                *eq = '\0';
                env_set(argv[i], eq + 1);
                *eq = '=';
            }
        }
        return 0;
    }

    /* ── Built-in: env ─────────────────────────────────────────────── */
    if (strcmp(argv[0], "env") == 0) {
        for (int i = 0; i < env_count; i++) puts(env_store[i]);
        return 0;
    }

    /* ── Built-in: help ────────────────────────────────────────────── */
    if (strcmp(argv[0], "help") == 0) {
        puts("tiny-x shell - built-in commands:");
        puts("  cd [dir]       - change directory");
        puts("  echo [args]    - print arguments");
        puts("  pwd            - print working directory");
        puts("  export VAR=VAL - set environment variable");
        puts("  env            - list environment");
        puts("  exit [code]    - exit shell");
        puts("  help           - this message");
        return 0;
    }

    /* ── External command ──────────────────────────────────────────── */
    char full_path[512];
    if (!find_exec(argv[0], full_path, sizeof(full_path))) {
        fprintf(stderr, "%s: command not found\n", argv[0]);
        return 127;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        return 1;
    }

    if (pid == 0) {
        /* Child: execute the program */
        execve(full_path, (char *const *)argv, (char *const *)environ_buf);
        fprintf(stderr, "execve: %s: %s\n", full_path, strerror(errno));
        _exit(127);
    }

    /* Parent: wait for child */
    if (!bg) {
        int status;
        wait4(pid, &status, 0, 0);
        last_exit = status;
        return status;
    }
    printf("[%d] running %s\n", pid, argv[0]);
    return 0;
}

/* ── Handle I/O redirection and pipes ────────────────────────────────── */
static int run_line(char *line);

static int run_pipe(char *left, char *right)
{
    /* Create a pipe */
    int pfd[2];
    if (pipe(pfd) < 0) {
        perror("pipe");
        return 1;
    }

    pid_t pid1 = fork();
    if (pid1 == 0) {
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);
        run_line(left);
        _exit(0);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        close(pfd[1]);
        dup2(pfd[0], STDIN_FILENO);
        close(pfd[0]);
        run_line(right);
        _exit(0);
    }

    close(pfd[0]);
    close(pfd[1]);
    wait4(pid1, 0, 0, 0);
    int status;
    wait4(pid2, &status, 0, 0);
    return status;
}

static int run_line(char *line)
{
    /* Trim whitespace */
    while (*line == ' ' || *line == '\t') line++;
    if (!*line || *line == '#') return 0;

    /* Check for pipe */
    char *pipe_pos = strchr(line, '|');
    if (pipe_pos) {
        *pipe_pos = '\0';
        return run_pipe(line, pipe_pos + 1);
    }

    /* Handle I/O redirection */
    char *redir_out  = 0, *redir_in = 0;
    int   redir_append = 0;
    char *p = line;
    while (*p) {
        if (*p == '>' && *(p+1) == '>') {
            *p = '\0'; redir_out = p + 2; redir_append = 1;
            while (*redir_out == ' ') redir_out++;
            break;
        }
        if (*p == '>') {
            *p = '\0'; redir_out = p + 1;
            while (*redir_out == ' ') redir_out++;
            break;
        }
        if (*p == '<') {
            *p = '\0'; redir_in = p + 1;
            while (*redir_in == ' ') redir_in++;
            break;
        }
        p++;
    }

    /* Tokenize */
    char *argv[MAX_ARGS];
    int argc = tokenize(line, argv, MAX_ARGS);
    if (!argc) return 0;

    /* Background execution (&) */
    int bg = 0;
    if (argc > 0 && strcmp(argv[argc-1], "&") == 0) {
        bg = 1; argv[--argc] = 0;
    }
    if (!argc) return 0;

    /* Apply redirections in child if needed */
    if (redir_out || redir_in) {
        pid_t pid = fork();
        if (pid == 0) {
            if (redir_out) {
                int flags = O_WRONLY | O_CREAT | (redir_append ? O_APPEND : O_TRUNC);
                int fd = open(redir_out, flags, 0644);
                if (fd < 0) { perror(redir_out); _exit(1); }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
            if (redir_in) {
                int fd = open(redir_in, O_RDONLY, 0);
                if (fd < 0) { perror(redir_in); _exit(1); }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            run_command(argv, argc, 0);
            _exit(0);
        }
        int status;
        wait4(pid, &status, 0, 0);
        return status;
    }

    return run_command(argv, argc, bg);
}

/* ── Main ────────────────────────────────────────────────────────────── */
int main(int argc, char **argv, char **envp)
{
    (void)argc; (void)argv;

    /* Initialize environment */
    env_count = 0;
    if (envp) {
        for (int i = 0; envp[i] && env_count < 128; i++) {
            strncpy(env_store[env_count], envp[i], 255);
            environ_buf[env_count] = env_store[env_count];
            env_count++;
        }
        environ_buf[env_count] = 0;
    }

    /* Default environment */
    if (!env_get("PATH")) env_set("PATH", "/bin:/usr/bin");
    if (!env_get("HOME")) env_set("HOME", "/");
    if (!env_get("PS1"))  env_set("PS1", PROMPT);

    /* Print welcome message */
    printf("\nWelcome to tiny-x shell!\n");
    printf("Type 'help' for built-in commands.\n\n");

    /* Main REPL loop */
    char line[MAX_LINE];
    for (;;) {
        /* Print prompt */
        const char *prompt = env_get("PS1");
        if (!prompt) prompt = PROMPT;

        write(STDOUT_FILENO, prompt, strlen(prompt));
        fflush(stdout);

        int n = readline(line, sizeof(line));
        if (n < 0) {
            /* EOF: Ctrl+D */
            puts("\nexit");
            break;
        }
        if (n == 0) continue;

        history_add(line);
        last_exit = run_line(line);
    }

    return last_exit;
}
