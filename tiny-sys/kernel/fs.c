#include "fs.h"

#define FS_MAX_NODES 128

enum fs_type {
	FS_DIR,
	FS_FILE,
};

struct fs_node {
	char name[FS_NAME_MAX];
	enum fs_type type;
	struct fs_node *parent;
	struct fs_node *first_child;
	struct fs_node *next_sibling;
};

static struct fs_node fs_pool[FS_MAX_NODES];
static size_t fs_pool_used;
static struct fs_node *fs_root;
static struct fs_node *fs_cwd;

static struct fs_node *fs_alloc(void)
{
	if (fs_pool_used >= FS_MAX_NODES)
		return NULL;
	struct fs_node *node = &fs_pool[fs_pool_used++];
	memset(node, 0, sizeof(*node));
	return node;
}

static struct fs_node *fs_find_child(struct fs_node *dir, const char *name)
{
	for (struct fs_node *c = dir->first_child; c; c = c->next_sibling) {
		if (strcmp(c->name, name) == 0)
			return c;
	}
	return NULL;
}

static int fs_next_component(const char **path, char *name)
{
	while (**path == '/')
		(*path)++;

	if (!**path)
		return 0;

	size_t i = 0;
	while (**path && **path != '/') {
		if (i + 1 >= FS_NAME_MAX)
			return -1;
		name[i++] = *(*path)++;
	}
	name[i] = '\0';
	return 1;
}

static struct fs_node *fs_resolve_dir(const char *path, struct fs_node *base)
{
	struct fs_node *cur;
	char comp[FS_NAME_MAX];

	if (!path || !path[0])
		return fs_cwd;

	if (path[0] == '/')
		cur = fs_root;
	else
		cur = base;

	while (fs_next_component(&path, comp) == 1) {
		if (comp[0] == '\0')
			continue;
		if (strcmp(comp, ".") == 0)
			continue;
		if (strcmp(comp, "..") == 0) {
			if (cur->parent)
				cur = cur->parent;
			continue;
		}

		struct fs_node *child = fs_find_child(cur, comp);
		if (!child || child->type != FS_DIR)
			return NULL;
		cur = child;
	}

	return cur;
}

static struct fs_node *fs_resolve_node(const char *path)
{
	char comp[FS_NAME_MAX];
	struct fs_node *cur;

	if (!path || !path[0])
		return NULL;

	if (path[0] == '/')
		cur = fs_root;
	else
		cur = fs_cwd;

	while (fs_next_component(&path, comp) == 1) {
		if (comp[0] == '\0')
			continue;
		if (strcmp(comp, ".") == 0)
			continue;
		if (strcmp(comp, "..") == 0) {
			if (cur->parent)
				cur = cur->parent;
			continue;
		}

		struct fs_node *child = fs_find_child(cur, comp);
		if (!child)
			return NULL;
		cur = child;
	}

	return cur;
}

static int fs_resolve_parent(const char *path, struct fs_node **parent, char *name)
{
	char buf[256];
	size_t len = strlen(path);

	if (len == 0 || len >= sizeof(buf))
		return -1;

	strcpy(buf, path);

	char *end = buf + len - 1;
	while (end > buf && *end == '/')
		*end-- = '\0';

	char *slash = NULL;
	for (char *p = buf; *p; p++) {
		if (*p == '/')
			slash = p;
	}

	if (!slash) {
		*parent = fs_cwd;
		if (strlen(buf) >= FS_NAME_MAX)
			return -1;
		strcpy(name, buf);
		return 0;
	}

	*slash = '\0';
	char *leaf = slash + 1;
	while (*leaf == '/')
		leaf++;

	if (!*leaf || strlen(leaf) >= FS_NAME_MAX)
		return -1;

	strcpy(name, leaf);

	if (buf[0] == '\0')
		*parent = fs_root;
	else
		*parent = fs_resolve_dir(buf, fs_cwd);

	return *parent ? 0 : -1;
}

static void fs_add_child(struct fs_node *parent, struct fs_node *child)
{
	child->parent = parent;
	child->next_sibling = parent->first_child;
	parent->first_child = child;
}

static void fs_unlink(struct fs_node *node)
{
	struct fs_node *parent = node->parent;

	if (!parent)
		return;

	if (parent->first_child == node) {
		parent->first_child = node->next_sibling;
		return;
	}

	for (struct fs_node *c = parent->first_child; c; c = c->next_sibling) {
		if (c->next_sibling == node) {
			c->next_sibling = node->next_sibling;
			return;
		}
	}
}

static struct fs_node *fs_create_dir(struct fs_node *parent, const char *name)
{
	if (fs_find_child(parent, name))
		return NULL;

	struct fs_node *node = fs_alloc();
	if (!node)
		return NULL;

	strcpy(node->name, name);
	node->type = FS_DIR;
	fs_add_child(parent, node);
	return node;
}

static struct fs_node *fs_create_file(struct fs_node *parent, const char *name)
{
	if (fs_find_child(parent, name))
		return NULL;

	struct fs_node *node = fs_alloc();
	if (!node)
		return NULL;

	strcpy(node->name, name);
	node->type = FS_FILE;
	fs_add_child(parent, node);
	return node;
}

void fs_init(void)
{
	fs_pool_used = 0;
	fs_root = fs_alloc();
	strcpy(fs_root->name, "/");
	fs_root->type = FS_DIR;
	fs_cwd = fs_root;
}

int fs_cd(const char *path)
{
	struct fs_node *dir;

	if (!path || !path[0]) {
		fs_cwd = fs_root;
		return 0;
	}

	dir = fs_resolve_dir(path, fs_cwd);
	if (!dir) {
		terminal_writestring("cd: no such directory\n");
		return -1;
	}

	fs_cwd = dir;
	return 0;
}

int fs_mkdir(const char *path)
{
	struct fs_node *parent;
	char name[FS_NAME_MAX];

	if (!path || !path[0]) {
		terminal_writestring("mkdir: missing path\n");
		return -1;
	}

	if (fs_resolve_parent(path, &parent, name) != 0) {
		terminal_writestring("mkdir: invalid path\n");
		return -1;
	}

	if (fs_find_child(parent, name)) {
		terminal_writestring("mkdir: already exists\n");
		return -1;
	}

	if (!fs_create_dir(parent, name)) {
		terminal_writestring("mkdir: out of space\n");
		return -1;
	}

	return 0;
}

int fs_touch(const char *path)
{
	struct fs_node *parent;
	char name[FS_NAME_MAX];
	struct fs_node *existing;

	if (!path || !path[0]) {
		terminal_writestring("touch: missing path\n");
		return -1;
	}

	if (fs_resolve_parent(path, &parent, name) != 0) {
		terminal_writestring("touch: invalid path\n");
		return -1;
	}

	existing = fs_find_child(parent, name);
	if (existing) {
		if (existing->type == FS_DIR) {
			terminal_writestring("touch: is a directory\n");
			return -1;
		}
		return 0;
	}

	if (!fs_create_file(parent, name)) {
		terminal_writestring("touch: out of space\n");
		return -1;
	}

	return 0;
}

int fs_rm(const char *path)
{
	struct fs_node *node;

	if (!path || !path[0]) {
		terminal_writestring("rm: missing path\n");
		return -1;
	}

	node = fs_resolve_node(path);
	if (!node) {
		terminal_writestring("rm: no such file or directory\n");
		return -1;
	}

	if (node == fs_root) {
		terminal_writestring("rm: cannot remove root\n");
		return -1;
	}

	if (node->type == FS_DIR && node->first_child) {
		terminal_writestring("rm: directory not empty\n");
		return -1;
	}

	fs_unlink(node);
	return 0;
}

static void fs_print_path(struct fs_node *node)
{
	char path[256];
	char names[16][FS_NAME_MAX];
	int count = 0;
	struct fs_node *cur = node;
	size_t pos;

	while (cur && cur->parent) {
		if (count >= 16)
			return;
		strcpy(names[count++], cur->name);
		cur = cur->parent;
	}

	path[0] = '/';
	path[1] = '\0';
	pos = 1;

	for (int i = count - 1; i >= 0; i--) {
		size_t len = strlen(names[i]);

		if (pos + len + 2 >= sizeof(path))
			return;
		if (i != count - 1)
			path[pos++] = '/';
		strcpy(path + pos, names[i]);
		pos += len;
	}

	if (node->type == FS_DIR) {
		if (pos + 2 >= sizeof(path))
			return;
		path[pos++] = '/';
	}

	path[pos] = '\0';
	terminal_writestring(path);
}

void fs_ls(const char *path)
{
	struct fs_node *dir = path && path[0] ? fs_resolve_dir(path, fs_cwd) : fs_cwd;

	if (!dir) {
		terminal_writestring("ls: no such directory\n");
		return;
	}

	if (dir->type != FS_DIR) {
		terminal_writestring("ls: not a directory\n");
		return;
	}

	if (!dir->first_child) {
		terminal_putchar('\n');
		return;
	}

	for (struct fs_node *c = dir->first_child; c; c = c->next_sibling) {
		fs_print_path(c);
		terminal_putchar('\n');
	}
}
