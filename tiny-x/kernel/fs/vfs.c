/*
 * vfs.c - Virtual Filesystem
 *
 * A simple in-memory filesystem tree.
 * Nodes are backed by data pointers (initramfs, or dynamically allocated).
 */

#include "vfs.h"
#include "../mm/vmm.h"
#include <string.h>

#define MAX_NODES   256

static vfs_node_t node_pool[MAX_NODES];
static int        node_used[MAX_NODES];
static vfs_node_t *vfs_root_node = 0;

/* ── Default read/write for in-memory files ──────────────────────────── */
static ssize_t mem_read(vfs_node_t *node, void *buf, size_t len, size_t offset)
{
    if (!node->data) return 0;
    if (offset >= node->size) return 0;
    size_t avail = node->size - offset;
    size_t n = (len < avail) ? len : avail;
    memcpy(buf, (uint8_t *)node->data + offset, n);
    return (ssize_t)n;
}

static ssize_t mem_write(vfs_node_t *node, const void *buf, size_t len, size_t offset)
{
    (void)node; (void)buf; (void)len; (void)offset;
    return -1;  /* read-only for initramfs */
}

static int mem_readdir(vfs_node_t *node, int idx, char *name, size_t namesz)
{
    vfs_node_t *child = node->children;
    int i = 0;
    while (child) {
        if (i == idx) {
            size_t len = __builtin_strlen(child->name);
            if (len >= namesz) len = namesz - 1;
            memcpy(name, child->name, len);
            name[len] = '\0';
            return 1;
        }
        child = child->sibling;
        i++;
    }
    return 0;
}

/* ── Allocate a VFS node ─────────────────────────────────────────────── */
static vfs_node_t *node_alloc(void)
{
    for (int i = 0; i < MAX_NODES; i++) {
        if (!node_used[i]) {
            node_used[i] = 1;
            vfs_node_t *n = &node_pool[i];
            memset(n, 0, sizeof(*n));
            return n;
        }
    }
    return 0;
}

void vfs_init(void)
{
    memset(node_pool, 0, sizeof(node_pool));
    memset(node_used, 0, sizeof(node_used));

    /* Create root directory '/' */
    vfs_root_node = node_alloc();
    vfs_root_node->name[0]  = '/';
    vfs_root_node->type     = VFS_DIR;
    vfs_root_node->readdir  = mem_readdir;
}

vfs_node_t *vfs_root(void) { return vfs_root_node; }

vfs_node_t *vfs_create(vfs_node_t *parent, const char *name,
                        uint32_t type, void *data, size_t size)
{
    if (!parent) parent = vfs_root_node;
    vfs_node_t *n = node_alloc();
    if (!n) return 0;

    size_t nlen = __builtin_strlen(name);
    if (nlen > VFS_NAME_MAX) nlen = VFS_NAME_MAX;
    memcpy(n->name, name, nlen);
    n->name[nlen] = '\0';

    n->type   = type;
    n->data   = data;
    n->size   = size;
    n->parent = parent;
    n->read   = (type == VFS_FILE) ? mem_read  : 0;
    n->write  = (type == VFS_FILE) ? mem_write : 0;
    n->readdir = (type == VFS_DIR)  ? mem_readdir : 0;

    /* Add as first child of parent */
    n->sibling      = parent->children;
    parent->children = n;

    return n;
}

/* Path lookup: handles absolute paths like /bin/sh */
vfs_node_t *vfs_lookup(const char *path)
{
    if (!path || path[0] != '/') return 0;

    vfs_node_t *node = vfs_root_node;
    const char *p = path + 1;  /* skip leading '/' */

    while (*p) {
        /* Extract component */
        char comp[VFS_NAME_MAX + 1];
        int i = 0;
        while (*p && *p != '/' && i < VFS_NAME_MAX)
            comp[i++] = *p++;
        comp[i] = '\0';
        if (*p == '/') p++;

        if (comp[0] == '\0') continue;  /* double slash */

        /* Search children */
        vfs_node_t *child = node->children;
        while (child) {
            if (__builtin_strcmp(child->name, comp) == 0) break;
            child = child->sibling;
        }
        if (!child) return 0;
        node = child;
    }
    return node;
}

ssize_t vfs_read(vfs_node_t *node, void *buf, size_t len, size_t offset)
{
    if (!node || node->type != VFS_FILE || !node->read) return -1;
    return node->read(node, buf, len, offset);
}

ssize_t vfs_write(vfs_node_t *node, const void *buf, size_t len, size_t offset)
{
    if (!node || !node->write) return -1;
    return node->write(node, buf, len, offset);
}

int vfs_mount(const char *path, vfs_node_t *root)
{
    /* Simplified: mount by adding root's children to mount point */
    vfs_node_t *mount_point = vfs_lookup(path);
    if (!mount_point) {
        /* Create the mount point */
        /* For now just return */
        return -1;
    }
    /* Graft root's children onto mount_point */
    if (root->children) {
        vfs_node_t *last = root->children;
        while (last->sibling) last = last->sibling;
        last->sibling         = mount_point->children;
        mount_point->children = root->children;
    }
    return 0;
}
