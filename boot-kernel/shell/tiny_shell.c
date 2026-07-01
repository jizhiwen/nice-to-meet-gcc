/*
 * tiny_shell — minimal PID 1 / interactive shell without libc.
 * Runs busybox applets via execve (/bin/ls -> busybox).
 *
 * Build: see 02-build.sh tinysh
 */
typedef long ssize_t;
typedef long pid_t;

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define O_RDONLY 0

#define SYS_read    0
#define SYS_write   1
#define SYS_open    2
#define SYS_close   3
#define SYS_fork    57
#define SYS_execve  59
#define SYS_exit    60
#define SYS_wait4   61
#define SYS_getpid  39
#define SYS_chdir   80
#define SYS_mkdir   83
#define SYS_mount   165

#define MAX_LINE  256
#define MAX_ARGS  16

static long sys1(long n, long a)
{
	long ret;
	__asm__ volatile("syscall"
			 : "=a"(ret)
			 : "a"(n), "D"(a)
			 : "rcx", "r11", "memory");
	return ret;
}

static long sys3(long n, long a, long b, long c)
{
	long ret;
	__asm__ volatile("syscall"
			 : "=a"(ret)
			 : "a"(n), "D"(a), "S"(b), "d"(c)
			 : "rcx", "r11", "memory");
	return ret;
}

static long sys4(long n, long a, long b, long c, long d)
{
	long ret;
	register long r10 __asm__("r10") = d;
	__asm__ volatile("syscall"
			 : "=a"(ret)
			 : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10)
			 : "rcx", "r11", "memory");
	return ret;
}

static ssize_t write_fd(int fd, const char *buf, long len)
{
	return (ssize_t)sys3(SYS_write, fd, (long)buf, len);
}

static void print(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	write_fd(STDOUT_FILENO, s, p - s);
}

static int char_eq(const char *s, const char *t)
{
	while (*s && *t) {
		if (*s != *t)
			return 0;
		s++;
		t++;
	}
	return *s == *t;
}

static void trim(char *s)
{
	char *p = s;
	while (*p == ' ' || *p == '\t')
		p++;
	if (p != s) {
		char *d = s;
		while (*p)
			*d++ = *p++;
		*d = '\0';
	}
}

static int split_args(char *line, char *argv[], int max)
{
	int n = 0;
	char *p = line;

	while (*p == ' ' || *p == '\t')
		p++;
	while (*p && n < max - 1) {
		argv[n++] = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		if (!*p)
			break;
		*p++ = '\0';
		while (*p == ' ' || *p == '\t')
			p++;
	}
	argv[n] = 0;
	return n;
}

static long sys0_fork(void)
{
	return sys1(SYS_fork, 0);
}

static long sys5_mount(const char *src, const char *tgt, const char *type)
{
	return sys4(SYS_mount, (long)src, (long)tgt, (long)type, 0);
}

static void resolve_path(const char *cmd, char *out, long out_len)
{
	long i = 0;
	long j;

	if (cmd[0] == '/') {
		while (cmd[i] && i + 1 < out_len) {
			out[i] = cmd[i];
			i++;
		}
		out[i] = '\0';
		return;
	}
	{
		const char prefix[] = "/bin/";
		for (j = 0; prefix[j] && i + 1 < out_len; j++, i++)
			out[i] = prefix[j];
	}
	for (j = 0; cmd[j] && i + 1 < out_len; j++, i++)
		out[i] = cmd[j];
	out[i] = '\0';
}

static void setup_rootfs(void)
{
	sys3(SYS_mkdir, (long)"/proc", 0755, 0);
	sys3(SYS_mkdir, (long)"/sys", 0755, 0);
	sys3(SYS_mkdir, (long)"/tmp", 0755, 0);
	sys3(SYS_mkdir, (long)"/dev", 0755, 0);

	sys5_mount("proc", "/proc", "proc");
	sys5_mount("sysfs", "/sys", "sysfs");
	sys5_mount("tmpfs", "/tmp", "tmpfs");
	sys5_mount("devtmpfs", "/dev", "devtmpfs");
}

static void run_line(char *line)
{
	char *argv[MAX_ARGS];
	char path[128];
	char *envp[] = { "PATH=/bin", 0 };
	int argc;
	pid_t pid;
	long status = 0;

	trim(line);
	if (!line[0])
		return;

	argc = split_args(line, argv, MAX_ARGS);
	if (argc == 0)
		return;

	if (char_eq(argv[0], "exit")) {
		sys1(SYS_exit, 0);
	}

	if (char_eq(argv[0], "cd")) {
		const char *dir = "/";
		if (argc >= 2)
			dir = argv[1];
		if (sys1(SYS_chdir, (long)dir) < 0) {
			print("tinysh: cd: ");
			print(dir);
			print("\n");
		}
		return;
	}

	resolve_path(argv[0], path, sizeof(path));
	pid = sys0_fork();
	if (pid == 0) {
		sys3(SYS_execve, (long)path, (long)argv, (long)envp);
		print("tinysh: exec failed: ");
		print(path);
		print("\n");
		sys1(SYS_exit, 127);
	}
	if (pid > 0)
		sys4(SYS_wait4, pid, (long)&status, 0, 0);
	else
		print("tinysh: fork failed\n");
}

static void read_line(char *buf, long cap)
{
	long pos = 0;
	char c;

	while (pos + 1 < cap) {
		ssize_t n = (ssize_t)sys3(SYS_read, STDIN_FILENO, (long)&c, 1);
		if (n <= 0)
			break;
		if (c == '\n' || c == '\r') {
			buf[pos] = '\0';
			print("\n");
			return;
		}
		buf[pos++] = c;
	}
	buf[pos] = '\0';
}

int main(void)
{
	char line[MAX_LINE];

	if (sys1(SYS_getpid, 0) == 1) {
		setup_rootfs();
		print("boot_kernel tinysh: no libc — busybox applets via execve\n");
		print("Type 'exit' to halt.\n");
	}

	for (;;) {
		print("tinysh# ");
		read_line(line, MAX_LINE);
		run_line(line);
	}
	return 0;
}
