#ifndef TYPE_H
#define TYPE_H

#include <ext2fs/ext2_fs.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

typedef struct ext2_super_block SUPER;
typedef struct ext2_group_desc GD;
typedef struct ext2_inode INODE;
typedef struct ext2_dir_entry_2 DIR;

SUPER *sp;
GD *gp;
INODE *ip;
DIR *dp;

#define FREE 0
#define READY 1

#define BLKSIZE 1024

#define NMINODE 100
#define NFD 16
#define NPROC 4

typedef struct minode {
  INODE INODE;
  int dev, ino;
  int refCount;
  int dirty;

  int mounted;
  int parent_mount;  // only initialized/used when mounted

  struct mntable *mptr;
} MINODE;

typedef struct oft {
  int mode;
  int refCount;
  MINODE *mptr;
  int offset;
} OFT;

typedef struct proc {
  struct proc *next;
  int pid;
  int ppid;
  int status;
  int uid, gid;
  MINODE *cwd;
  OFT *fd[NFD];
} PROC;

struct mntable {
  int ninodes;
  int nblocks;
  int bmap;
  int imap;
  int iblock;
  int dev, busy;

  struct minode *mounted_inode;

  struct ext2_inode *inode_tbl;
  uint32_t inode_tbl_size;
  uint32_t inode_tbl_blk;

  char name[256];
  char mount_name[64];
};

#endif