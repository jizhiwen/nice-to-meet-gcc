/*
 * initramfs.c - cpio newc format initramfs parser
 *
 * The initramfs is either:
 *   a) Embedded in the kernel (between initramfs_start/initramfs_end)
 *   b) Passed by GRUB as a Multiboot2 module
 *
 * cpio newc format:
 *   - 6-byte magic: "070701" (or "070702" with checksum)
 *   - 104 bytes of ASCII hex fields
 *   - Filename (c_namesize bytes, NUL-terminated)
 *   - Padding to 4-byte boundary
 *   - File data (c_filesize bytes)
 *   - Padding to 4-byte boundary
 *   - Last entry is named "TRAILER!!!"
 */

#include "initramfs.h"
#include "vfs.h"
#include <string.h>

/* cpio newc header (110 bytes) */
typedef struct __attribute__((packed)) {
    char c_magic[6];        /* "070701" */
    char c_ino[8];
    char c_mode[8];
    char c_uid[8];
    char c_gid[8];
    char c_nlink[8];
    char c_mtime[8];
    char c_filesize[8];
    char c_devmajor[8];
    char c_devminor[8];
    char c_rdevmajor[8];
    char c_rdevminor[8];
    char c_namesize[8];
    char c_check[8];
} cpio_header_t;

static uint32_t hex8(const char *s)
{
    uint32_t v = 0;
    for (int i = 0; i < 8; i++) {
        char c = s[i];
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else break;
        v = (v << 4) | d;
    }
    return v;
}

static uint32_t align4(uint32_t v) { return (v + 3) & ~3U; }

/* Create directory path components in VFS */
static vfs_node_t *vfs_mkdir_p(const char *path)
{
    vfs_node_t *node = vfs_root();
    const char *p = path;
    if (*p == '/') p++;

    char comp[256];
    while (*p) {
        int i = 0;
        while (*p && *p != '/') comp[i++] = *p++;
        comp[i] = '\0';
        if (*p == '/') p++;
        if (!comp[0]) continue;

        /* Look for existing child */
        vfs_node_t *child = node->children;
        while (child) {
            if (__builtin_strcmp(child->name, comp) == 0) break;
            child = child->sibling;
        }
        if (!child) {
            child = vfs_create(node, comp, VFS_DIR, 0, 0);
        }
        node = child;
    }
    return node;
}

void initramfs_load(void *data, size_t size)
{
    uint8_t *p   = (uint8_t *)data;
    uint8_t *end = p + size;

    while (p + sizeof(cpio_header_t) <= end) {
        cpio_header_t *hdr = (cpio_header_t *)p;

        /* Check magic */
        if (memcmp(hdr->c_magic, "070701", 6) != 0 &&
            memcmp(hdr->c_magic, "070702", 6) != 0) break;

        uint32_t namesize = hex8(hdr->c_namesize);
        uint32_t filesize = hex8(hdr->c_filesize);
        uint32_t mode     = hex8(hdr->c_mode);

        p += sizeof(cpio_header_t);

        /* Filename */
        if (p + namesize > end) break;
        char *filename = (char *)p;

        /* Check for trailer */
        if (namesize >= 11 && memcmp(filename, "TRAILER!!!", 10) == 0) break;

        p += align4(sizeof(cpio_header_t) + namesize) - sizeof(cpio_header_t);

        /* File data */
        uint8_t *filedata = p;
        if (p + filesize > end) break;

        /* Determine file type from mode */
        uint32_t ftype = (mode >> 12) & 0xF;
        /* S_IFREG=8, S_IFDIR=4, S_IFLNK=10 */

        if (filename[0] && !(namesize == 2 && filename[0] == '.')) {
            /* Build full path: ensure leading slash */
            char full_path[VFS_PATH_MAX];
            if (filename[0] == '/') {
                __builtin_strncpy(full_path, filename, VFS_PATH_MAX-1);
            } else {
                full_path[0] = '/';
                __builtin_strncpy(full_path + 1, filename, VFS_PATH_MAX - 2);
            }
            full_path[VFS_PATH_MAX-1] = '\0';

            /* Find parent directory */
            char parent_path[VFS_PATH_MAX];
            __builtin_strncpy(parent_path, full_path, VFS_PATH_MAX-1);
            parent_path[VFS_PATH_MAX-1] = '\0';
            /* Find last '/' */
            char *last_slash = parent_path;
            for (char *q = parent_path; *q; q++) if (*q == '/') last_slash = q;
            *last_slash = '\0';  /* truncate to parent */
            const char *base = full_path + (last_slash - parent_path) + 1;
            if (!base[0]) goto next;

            vfs_node_t *parent_dir;
            if (last_slash == parent_path) {
                parent_dir = vfs_root();
            } else {
                parent_path[0] = '/';  /* restore root slash */
                parent_dir = vfs_mkdir_p(parent_path);
            }
            if (!parent_dir) goto next;

            if (ftype == 8) {
                /* Regular file */
                vfs_create(parent_dir, base, VFS_FILE, filedata, filesize);
            } else if (ftype == 4) {
                /* Directory */
                vfs_create(parent_dir, base, VFS_DIR, 0, 0);
            }
        }
    next:
        p = filedata + align4(filesize);
    }
}
