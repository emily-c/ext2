#ifndef ALLOC_H
#define ALLOC_H

int ialloc(int dev);
int decFreeInodes(int dev);
int incFreeBlocks(int dev);
int decFreeBlocks(int dev);
void idealloc(int dev, int ino);
int balloc(int dev);
int bdealloc(int dev, int bit);

#endif