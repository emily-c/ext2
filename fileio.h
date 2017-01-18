#ifndef OPEN_CLOSE_LSEEK_H
#define OPEN_CLOSE_LSEEK_H

#include "type.h"

enum open_flags { R, W, RW, APPEND };

int loc_open(char *filename, enum open_flags flags);
int loc_close(int fd);
int loc_read(int fd, char buf[], int nbytes);
int loc_write(int fd, char buf[], int nbytes);
void loc_lseek(int fd, int offset);

void cat(char *filename);
void cp(char *src, char *dst);
void mv(char *src, char *dst);

void truncat(MINODE *mip);

#endif