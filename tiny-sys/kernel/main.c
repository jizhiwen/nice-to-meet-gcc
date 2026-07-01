#include "kernel.h"
#include "fs.h"

void kernel_main(uint64_t multiboot_magic, uint64_t multiboot_info)
{
	(void)multiboot_info;

	serial_init();
	terminal_init();

	if (multiboot_magic == 0x36d76289)
		terminal_writestring("tiny-sys kernel started (multiboot2).\n");
	else
		terminal_writestring("tiny-sys kernel started.\n");

	fs_init();
	shell_run();
}
