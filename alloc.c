#include "alloc.h"
#include "mount.h"
#include "type.h"
#include "util.h"

int tst_bit(char *buf, int bit) {
  int i, j;

  i = bit / 8;
  j = bit % 8;
  if (buf[i] & (1 << j)) {
    return 1;
  }
  return 0;
}

void clr_bit(char *buf, int bit) {
  int i, j;
  i = bit / 8;
  j = bit % 8;
  buf[i] &= ~(1 << j);
}

int set_bit(char *buf, int bit) {
  int i, j;
  i = bit / 8;
  j = bit % 8;
  buf[i] |= (1 << j);
  return 0;
}

int incFreeInodes(int dev) {
  char buf[BLKSIZE];

  // inc free inodes count in SUPER and GD
  get_block_buf(dev, 1, buf);
  sp = (SUPER *)buf;
  sp->s_free_inodes_count++;
  put_block(dev, 1, buf);

  get_block_buf(dev, 2, buf);
  gp = (GD *)buf;
  gp->bg_free_inodes_count++;
  put_block(dev, 2, buf);

  return 0;
}

int decFreeInodes(int dev) {
  char buf[BLKSIZE];

  // inc free inodes count in SUPER and GD
  get_block_buf(dev, 1, buf);
  sp = (SUPER *)buf;
  sp->s_free_inodes_count--;
  put_block(dev, 1, buf);

  get_block_buf(dev, 2, buf);
  gp = (GD *)buf;
  gp->bg_free_inodes_count--;
  put_block(dev, 2, buf);

  return 0;
}

int incFreeBlocks(int dev) {
  char buf[BLKSIZE];

  // inc free block count in SUPER and GD
  get_block_buf(dev, 1, buf);
  sp = (SUPER *)buf;
  sp->s_free_blocks_count++;
  put_block(dev, 1, buf);

  get_block_buf(dev, 2, buf);
  gp = (GD *)buf;
  gp->bg_free_blocks_count++;
  put_block(dev, 2, buf);

  return 0;
}

int decFreeBlocks(int dev) {
  char buf[BLKSIZE];

  get_block_buf(dev, 1, buf);
  sp = (SUPER *)buf;
  sp->s_free_blocks_count--;
  put_block(dev, 1, buf);

  get_block_buf(dev, 2, buf);
  gp = (GD *)buf;
  gp->bg_free_blocks_count--;
  put_block(dev, 2, buf);

  return 0;
}

#define RESERVED_INO_BITS 11

int ialloc(int dev) {
  int i;
  char buf[BLKSIZE] = {0};

  struct mntable *me = dev_to_mnt_entry(dev);

  // get inode Bitmap into buf
  get_block_buf(dev, me->imap, buf);

  for (i = RESERVED_INO_BITS; i < me->ninodes + RESERVED_INO_BITS; i++) {
    if (tst_bit(buf, i) == 0) {
      set_bit(buf, i);
      put_block(dev, me->imap, buf);

      // update free inode count in SUPER and GD
      decFreeInodes(dev);

      //   printf("ialloc: ino=%d\n", i + 1);
      return (i + 1);
    }
  }
  return 0;
}

void idealloc(int dev, int ino) {
  char buf[BLKSIZE];

  struct mntable *me = dev_to_mnt_entry(dev);

  if (ino > me->ninodes) {
    // printf("inumber %d out of range\n", ino);
    return;
  }

  // get inode bitmap block
  get_block_buf(dev, me->imap, buf);
  clr_bit(buf, ino - 1);

  // write buf back
  put_block(dev, me->imap, buf);

  // update free inode count in SUPER and GD
  incFreeInodes(dev);
}

int balloc(int dev) {
  int i;
  char buf[BLKSIZE];

  struct mntable *me = dev_to_mnt_entry(dev);

  get_block_buf(dev, me->bmap, buf);

  for (i = 0; i < me->nblocks; i++) {
    if (tst_bit(buf, i) == 0) {
      set_bit(buf, i);
      put_block(dev, me->bmap, buf);

      // update free inode count in SUPER and GD
      decFreeInodes(dev);

      //   printf("ialloc: ino=%d\n", i + 1);
      return (i + 1);
    }
  }
  return 0;
}

int bdealloc(int dev, int bit) {
  char buf[BLKSIZE];

  struct mntable *me = dev_to_mnt_entry(dev);
  get_block_buf(dev, me->bmap, buf);
  clr_bit(buf, bit);
  put_block(dev, me->bmap, buf);

  return 0;
}