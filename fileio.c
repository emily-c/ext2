#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>

#include "alloc.h"
#include "fileops.h"
#include "fileio.h"
#include "type.h"
#include "util.h"

extern PROC *running;

#define OFT_LEN NFD * 2
OFT oft[OFT_LEN];  // global oft table that all procs use

static int alloc_fd(OFT **fd, OFT **out_file) {
  int ret_fd = -1;

  // find open fd in proc tbl
  for (int i = 0; i < NFD; i++) {
    if (fd[i] == 0) {
      ret_fd = i;
      break;
    }
  }
  if (ret_fd == -1) return ret_fd;

  // find free fd in global oft table
  for (int i = 0; i < OFT_LEN; i++) {
    if (oft[i].mptr == NULL) {
      *out_file = &oft[i];
      break;
    }
  }

  // populate running fd table with cur oft file struct
  fd[ret_fd] = *out_file;
  return ret_fd;
}

static bool valid_fd(int fd) {
  return fd >= 0 && fd < NFD && running->fd[fd] != NULL;
}

int loc_open(char *filename, enum open_flags flags) {
  int dev = path_start_dev(filename);
  int ino = getino(&dev, filename);
  if (ino == 0) {
    return -1;
  }

  MINODE *mip = iget(dev, ino);

  // TODO: check file INODE's access permission here ...

  OFT *open_file = {0};
  int fd = alloc_fd(running->fd, &open_file);
  if (fd == -1) return fd;

  // initialize open_file OFT struct
  open_file->mptr = mip;
  open_file->mode = flags;
  if (flags == APPEND) {
    open_file->offset = mip->INODE.i_size;
  } else {
    open_file->offset = 0;
  }
  open_file->refCount++;

  return fd;
}

int loc_close(int fd) {
  if (valid_fd(fd)) {
    OFT *file = running->fd[fd];

    if (--file->refCount == 0) {
      iput(file->mptr);

      // free file
      oft[file - oft].mptr = NULL;
      running->fd[fd] = NULL;
    }
  }

  return -1;
}

void loc_lseek(int fd, int offset) {
  if (!valid_fd(fd)) return;

  OFT *file = running->fd[fd];
  file->offset = offset;
}

int logical_to_physical(OFT *file, int logical_blk, int initialize) {
  INODE *inode = &file->mptr->INODE;
  if (logical_blk < 12) {
    return initialize ? inode->i_block[logical_blk] = balloc(file->mptr->dev)
                      : inode->i_block[logical_blk];
  } else if (logical_blk >= 12 && logical_blk < 256 + 12) {
    if (logical_blk == 12 && initialize) {
      inode->i_block[12] = balloc(file->mptr->dev);
    }

    int ind_blks[256];
    logical_blk -= 12;
    get_block_buf(file->mptr->dev, inode->i_block[12], ind_blks);

    if (initialize) {
      ind_blks[logical_blk] = balloc(file->mptr->dev);
      put_block(file->mptr->dev, inode->i_block[12], (char *)ind_blks);
    }

    return ind_blks[logical_blk];
  } else {  // logical is >= 268
    if (logical_blk == 268 && initialize) {
      inode->i_block[13] = balloc(file->mptr->dev);
    }

    int dind_blks[256];
    logical_blk -= 268;
    get_block_buf(file->mptr->dev, inode->i_block[13], dind_blks);

    int dindex = logical_blk / 256;
    int doffset = logical_blk % 256;

    // create new top-level dindirect block to hold indirect ones
    if (doffset == 0 && initialize) {
      dind_blks[dindex] = balloc(file->mptr->dev);
      put_block(file->mptr->dev, inode->i_block[13], (char *)dind_blks);
    }

    int dind_sub[256];
    get_block_buf(file->mptr->dev, dind_blks[dindex], dind_sub);

    if (initialize) {
      dind_sub[doffset] = balloc(file->mptr->dev);
      put_block(file->mptr->dev, dind_blks[dindex], (char *)dind_sub);
    }

    return dind_sub[doffset];
  }
  return 0;
}

void truncat(MINODE *mip) {
  uint32_t *blocks = mip->INODE.i_block;

  for (int i = 0; blocks[i] && i < 12; i++) {
    bdealloc(mip->dev, blocks[i]);
  }

  if (blocks[12]) {
    uint32_t *ind_blocks = get_block(mip->dev, blocks[12]);
    for (int i = 0; ind_blocks[i] && i < 256; i++) {
      bdealloc(mip->dev, ind_blocks[i]);
    }
  }

  if (blocks[13]) {
    uint32_t dind_blocks[BLKSIZE];
    memcpy(dind_blocks, get_block(mip->dev, blocks[13]), BLKSIZE);
    for (int i = 0; dind_blocks[i] && i < 256; i++) {
      uint32_t *ind_blocks = get_block(mip->dev, dind_blocks[i]);
      for (int j = 0; ind_blocks[j] && j < 256; j++) {
        bdealloc(mip->dev, ind_blocks[i]);
      }
      bdealloc(mip->dev, dind_blocks[i]);
    }
  }

  bdealloc(mip->dev, blocks[12]);
  bdealloc(mip->dev, blocks[13]);
}

int _read(OFT *file, char buf[], int nbytes) {
  static char blk_buf[BLKSIZE];
  int count = 0;
  INODE *inode = &file->mptr->INODE;
  int avil = inode->i_size - file->offset;

  // if one data block is not enough, loop back to OUTER while for more ...
  while (nbytes && avil) {
    int lbk = file->offset / BLKSIZE;
    int startByte = file->offset % BLKSIZE;
    int blk = logical_to_physical(file, lbk, 0);

    get_block_buf(file->mptr->dev, blk, blk_buf);

    char *cp_start = blk_buf + startByte;
    int remain = BLKSIZE - startByte;

    int read_bytes = nbytes > remain ? remain : nbytes;
    if (avil < read_bytes) {
      read_bytes = avil;
      avil = 0;
    } else {
      avil -= read_bytes;
    }

    // !! OPTIMIZED !!
    memcpy(buf + count, cp_start, read_bytes);
    count += read_bytes;
    file->offset += read_bytes;
    nbytes -= read_bytes;
  }
  return count;
}

int _write(OFT *file, char buf[], int nbytes) {
  static char wbuf[BLKSIZE];
  MINODE *mip = file->mptr;
  int count = 0;

  // loop back to while to write more .... until nbytes are written
  while (nbytes > 0) {
    int lbk = file->offset / BLKSIZE;
    int startByte = file->offset % BLKSIZE;
    int blk = logical_to_physical(file, lbk, 1);

    get_block_buf(mip->dev, blk, wbuf);

    char *cp_start = wbuf + startByte;
    int remain = BLKSIZE - startByte;

    int write_bytes = nbytes > remain ? remain : nbytes;

    // !! OPTIMIZED !!
    memcpy(cp_start + count, buf + count, write_bytes);
    file->offset += write_bytes;
    count += write_bytes;
    nbytes -= write_bytes;

    if (file->offset > mip->INODE.i_size) mip->INODE.i_size = file->offset;

    put_block(mip->dev, blk, wbuf);  // write wbuf[ ] to disk
  }

  mip->dirty = 1;
  iput(mip);

  return nbytes;
}

void cp(char *src, char *dst) {
  char dst_name_buf[256];
  char buf[BLKSIZE];
  int n = 0;
  strcpy(dst_name_buf, dst);
  int fd = loc_open(src, 0);
  if (fd == -1) {
    err("copy source not found");
    return;
  }

  if (loc_stat(dst_name_buf).st_ino == 0) {
    strcpy(dst_name_buf, dst);
    loc_creat(dst_name_buf);
  }
  int gd = loc_open(dst, 1);
  while ((n = loc_read(fd, buf, BLKSIZE))) {
    loc_write(gd, buf, n);
  }
  loc_close(gd);
  loc_close(fd);
}

void mv(char *src, char *dst) {
  char src_buf[256], dst_buf[256];
  strcpy(src_buf, src);
  strcpy(dst_buf, dst);

  int src_dev = path_start_dev(src);
  int src_ino = getino(&src_dev, src_buf);
  if (src_ino == 0) {
    err("source does not exist");
    return;
  }
  // MINODE *smip = iget(src_dev, src_ino);

  int dst_dev = path_start_dev(src);
  int dst_ino = getino(&dst_dev, dst_buf);
  if (dst_ino != 0) {
    err("dest already exists");
    return;
  }
  if (dst_dev != src_dev) {
    char src_buf[256];
    strcpy(src_buf, src);

    cp(src, dst);
    loc_unlink(src_buf);
    return;
  }

  // MINODE *dmip = iget(dst_dev, dst_ino);

  strcpy(src_buf, src);
  strcpy(dst_buf, dst);
  loc_link(src_buf, dst_buf);
  loc_unlink(src);
}

void cat(char *file) {
  int fd = loc_open(file, 0);
  char buf[BLKSIZE] = {0};
  int n = 0;
  while ((n = loc_read(fd, buf, 1024))) {
    buf[n] = 0;
    printf("%s", buf);
  }
  loc_close(fd);
}

int loc_read(int fd, char buf[], int nbytes) {
  if (!valid_fd(fd)) return 0;

  OFT *file = running->fd[fd];
  // INODE *inode = &file->mptr->INODE;

  if (file->mode == R || file->mode == RW) {
    return _read(file, buf, nbytes);
  }

  return 0;
}

int loc_write(int fd, char buf[], int nbytes) {
  if (!valid_fd(fd)) return 0;

  OFT *file = running->fd[fd];
  // INODE *inode = &file->mptr->INODE;

  if (file->mode == W || file->mode == RW || file->mode == APPEND) {
    return _write(file, buf, nbytes);
  }

  return 0;
}
