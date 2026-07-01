#include "kernel.h"
#include "fs.h"

#define LINE_MAX 256

static inline void outb(uint16_t port, uint8_t val)
{
	__asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void shell_split(char *line, char **cmd, char **args)
{
	*cmd = line;

	while (*line && *line != ' ')
		line++;

	if (*line) {
		*line++ = '\0';
		while (*line == ' ')
			line++;
	}

	*args = line;
}

static void shell_reboot(void)
{
	terminal_writestring("Rebooting...\n");
	outb(0x64, 0xFE);
	for (;;)
		__asm__ volatile("hlt");
}

static void shell_help(void)
{
	terminal_writestring("Commands:\n");
	terminal_writestring("  help              show this message\n");
	terminal_writestring("  clear             clear the screen\n");
	terminal_writestring("  echo <text>       print text\n");
	terminal_writestring("  version           show kernel version\n");
	terminal_writestring("  ls [path]         list directory\n");
	terminal_writestring("  cd [path]         change directory\n");
	terminal_writestring("  mkdir <path>      create directory\n");
	terminal_writestring("  touch <path>      create empty file\n");
	terminal_writestring("  rm <path>         remove file or empty directory\n");
	terminal_writestring("  reboot            reboot the machine\n");
}

static void shell_echo(const char *args)
{
	if (*args)
		terminal_writestring(args);
	terminal_putchar('\n');
}

static void shell_version(void)
{
	terminal_writestring("tiny-sys 0.1 (x86_64)\n");
}

static void shell_execute(char *line)
{
	char *cmd;
	char *args;

	shell_split(line, &cmd, &args);

	if (*cmd == '\0')
		return;

	if (strcmp(cmd, "help") == 0) {
		shell_help();
	} else if (strcmp(cmd, "clear") == 0) {
		terminal_clear();
	} else if (strcmp(cmd, "version") == 0) {
		shell_version();
	} else if (strcmp(cmd, "reboot") == 0) {
		shell_reboot();
	} else if (strcmp(cmd, "ls") == 0) {
		fs_ls(*args ? args : NULL);
	} else if (strcmp(cmd, "cd") == 0) {
		fs_cd(*args ? args : "/");
	} else if (strcmp(cmd, "mkdir") == 0) {
		fs_mkdir(args);
	} else if (strcmp(cmd, "touch") == 0) {
		fs_touch(args);
	} else if (strcmp(cmd, "rm") == 0) {
		fs_rm(args);
	} else if (strcmp(cmd, "echo") == 0) {
		shell_echo(args);
	} else {
		terminal_writestring("Unknown command: ");
		terminal_writestring(cmd);
		terminal_putchar('\n');
	}
}

static void shell_readline(char *buf, size_t max)
{
	size_t len = 0;

	while (len + 1 < max) {
		char c = keyboard_getchar();

		if (c == '\n' || c == '\r') {
			buf[len] = '\0';
			terminal_putchar('\n');
			return;
		}

		if (c == '\b' || c == 127) {
			if (len > 0) {
				len--;
				terminal_putchar('\b');
				terminal_putchar(' ');
				terminal_putchar('\b');
			}
			continue;
		}

		buf[len++] = c;
		terminal_putchar(c);
	}

	buf[len] = '\0';
}

void shell_run(void)
{
	char line[LINE_MAX];

	terminal_writestring("Welcome to tiny-sys shell.\n");
	terminal_writestring("Type 'help' for available commands.\n\n");

	for (;;) {
		terminal_writestring("tiny-sys> ");
		shell_readline(line, sizeof(line));
		shell_execute(line);
	}
}
