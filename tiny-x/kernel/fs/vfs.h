#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include "include/types.h"

/* File types */
#define VFS_FILE    1
#define VFS_DIR     2
#define VFS_SYMLINK 3

#define VFS_NAME_MAX    255
#define VFS_PATH_MAX    1024

/* Forward declaration so function pointer typedefs can reference it */
struct vfs_node;

typedef ssize_t (*vfs_read_fn)(struct vfs_node *, void *, size_t, size_t);
typedef ssize_t (*vfs_write_fn)(struct vfs_node *, const void *, size_t, size_t);
typedef int     (*vfs_readdir_fn)(struct vfs_node *, int, char *, size_t);

/* VFS node */
struct vfs_node {
    char            name[VFS_NAME_MAX + 1];
    uint32_t        type;       /* VFS_FILE, VFS_DIR, etc. */
    uint64_t        size;       /* file size in bytes */
    uint32_t        mode;       /* permission bits */
    void           *data;       /* pointer to file data (in memory) */
    struct vfs_node *parent;
    struct vfs_node *children;  /* first child (for dirs) */
    struct vfs_node *sibling;   /* next sibling */

    vfs_read_fn     read;
    vfs_write_fn    write;
    vfs_readdir_fn  readdir;
};

typedef struct vfs_node vfs_node_t;

/* VFS operations */
void      vfs_init(void);
vfs_node_t *vfs_root(void);

/* Path lookup (absolute path from '/') */
vfs_node_t *vfs_lookup(const char *path);

/* Create a file/dir node */
vfs_node_t *vfs_create(vfs_node_t *parent, const char *name,
                        uint32_t type, void *data, size_t size);

/* Read from a file node */
ssize_t vfs_read(vfs_node_t *node, void *buf, size_t len, size_t offset);

/* Write to a file node */
ssize_t vfs_write(vfs_node_t *node, const void *buf, size_t len, size_t offset);

/* Mount a filesystem at a path */
int vfs_mount(const char *path, vfs_node_t *root);

#endif /* VFS_H */
