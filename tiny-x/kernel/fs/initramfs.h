#ifndef INITRAMFS_H
#define INITRAMFS_H

#include <stdint.h>
#include <stddef.h>

/*
 * Parse a newc-format cpio archive (used by Linux initramfs).
 * Extracts files into the VFS.
 *
 * cpio newc header magic: "070701"
 */
void initramfs_load(void *data, size_t size);

/* Symbols set by linker script for embedded initramfs */
extern uint8_t initramfs_start[];
extern uint8_t initramfs_end[];

#endif /* INITRAMFS_H */
