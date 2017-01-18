#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

// modes from http://www.nongnu.org/ext2-doc/ext2.html#I-MODE
#define EXT2_FT_UNKNOWN 0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR 2
#define EXT2_FT_CHRDEV 3
#define EXT2_FT_BLKDEV 4
#define EXT2_FT_FIFO 5
#define EXT2_FT_SOCK 6
#define EXT2_FT_SYMLINK 7
#define EXT2_FT_MAX 8

#define EXT2_S_IFMT 0xF000
#define EXT2_S_IFSOCK 0xC000
#define EXT2_S_IFLNK 0xA000
#define EXT2_S_IFREG 0x8000
#define EXT2_S_IFBLK 0x6000
#define EXT2_S_IFDIR 0x4000
#define EXT2_S_IFCHR 0x2000
#define EXT2_S_IFIFO 0x1000

#define EXT2_S_ISUID 0x0800
#define EXT2_S_ISGID 0x0400
#define EXT2_S_ISVTX 0x0200
#define EXT2_S_IRWXU 0x01C0
#define EXT2_S_IRUSR 0x0100
#define EXT2_S_IWUSR 0x0080
#define EXT2_S_IXUSR 0x0040
#define EXT2_S_IRWXG 0x0038
#define EXT2_S_IRGRP 0x0020
#define EXT2_S_IWGRP 0x0010
#define EXT2_S_IXGRP 0x0008
#define EXT2_S_IRWXO 0x0007
#define EXT2_S_IROTH 0x0004
#define EXT2_S_IWOTH 0x0002
#define EXT2_S_IXOTH 0x0001

// shell colors
#define REG_COL "\x1B[0m"
#define BLUE_COL "\x1B[34m"
#define CYAN_COL "\x1B[36m"
#define YELLOW_COL "\x1B[33m"
#define GREEN_COL "\x1B[32m"
#define PURPLE_COL "\x1B[35m"
#define RED_COL "\x1B[31m"

int printf(const char *format, ...);
#define err(msg) (printf("%serror: " msg "%s\n", RED_COL, REG_COL))

void *get_block(int fd, uint32_t blk_num);
void get_block_buf(int dev, int blk, void *buf);
void put_block(int dev, int blk, char *buf);

#endif