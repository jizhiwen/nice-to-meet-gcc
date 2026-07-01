#pragma once

#include "kernel.h"

#define FS_NAME_MAX 32

void fs_init(void);

int fs_cd(const char *path);
int fs_mkdir(const char *path);
int fs_touch(const char *path);
int fs_rm(const char *path);
void fs_ls(const char *path);
