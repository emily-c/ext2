#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stdio.h>
#include "type.h"

void init(void);
void mount_root(const char *fname);
void cd(char *path);
void ls(char *path);

int getino(int *d, char *path);
MINODE *iget(int dev, int ino);
void iput(MINODE *mip);

void switch_proc(int proc_num);
void list_proc(void);
void init_procs(void);

void pfd(void);
int path_start_dev(char *path);

size_t loc_readlink(char *pathname, uint32_t buf[15]);
struct stat loc_stat(char *path);
char *pwd(char *out_path);
void loc_link(char *old_name, char *new_name);

void loc_chmod(char *pathname, char *mode);
void loc_touch(char *pathname);

void loc_mkdir(char *path);
void loc_rmdir(char *path);
void loc_creat(char *path);
void loc_rm(char *path);

void loc_unlink(char *pathname);
void loc_symlink(char *old_name, char *new_name);
size_t loc_readlink(char *pathname, uint32_t buf[15]);

struct stat loc_stat(char *path);
void diagnostic(void);
void quit();

#endif