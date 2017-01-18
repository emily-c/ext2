#include "util.h"
#include <unistd.h>

void *get_block(int fd, uint32_t blk_num) {
  static uint8_t blk[1024];

  pread(fd, blk, 1024, blk_num * 1024);
  return blk;
}

void get_block_buf(int fd, int blk_num, void *buf) {
  pread(fd, buf, 1024, blk_num * 1024);
}

void put_block(int fd, int blk_num, char *buf) {
  pwrite(fd, buf, 1024, blk_num * 1024);
}