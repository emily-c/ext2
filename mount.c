#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "fileops.h"
#include "mount.h"
#include "type.h"
#include "util.h"

#define MOUNT_TBL_SIZE 8
struct mntable mount_tbl[MOUNT_TBL_SIZE];

extern MINODE minode[NMINODE];

int find_mnt_dev(int old_dev, int inode) {
  if (inode == 0) {
    return 0;
  }

  for (int i = 0; i < NMINODE; i++) {
    if (minode[i].mounted == 1 &&
        (minode[i].mptr->mounted_inode->ino == inode) &&
        (minode[i].mptr->dev != old_dev)) {
      return minode[i].mptr->dev;
    }
  }
  return 0;
}

void mount_list(void) {
  struct mntable *entry = mount_tbl;
  while (entry->dev != 0 && (entry - mount_tbl) < MOUNT_TBL_SIZE) {
    printf("%s -> %s\n", entry->name, entry->mount_name);
    entry++;
  }
}

int mount_fs(char *disk, char *path) {
  char path_buf[256];
  strcpy(path_buf, path);

  int dev = path_start_dev(path);
  int ino = getino(&dev, path_buf);
  if (ino == 0) {
    err("mount point does not exist");
    return 1;
  }
  MINODE *mip = iget(dev, ino);
  if ((mip->INODE.i_mode & EXT2_S_IFDIR) != EXT2_S_IFDIR) {
    err("mount point not a directory");
    return 1;
  }

  struct mntable *search = NULL, *entry = NULL;
  for (int i = 0; i < MOUNT_TBL_SIZE; i++) {
    search = mount_tbl + i;

    if (search->dev == 0) {
      entry = search;
      break;
    } else if (strcmp(disk, search->name) == 0) {
      err("filesystem already mounted");
      return 1;
    }
  }

  if (!entry) {
    err("too many mounted filesystems");
    return 1;
  }

  int new_dev = open(disk, O_RDWR);
  if (new_dev == -1) {
    err("disk image does not exist");
    return 1;
  }
  struct ext2_super_block *super_block =
      (struct ext2_super_block *)get_block(new_dev, 1);
  if (super_block->s_magic != EXT2_SUPER_MAGIC) {
    err("not a valid ext2 filesystem");
    return 1;
  }

  entry->ninodes = super_block->s_inodes_count;
  entry->nblocks = super_block->s_blocks_count;
  entry->dev = new_dev;
  entry->mounted_inode = mip;

  struct ext2_group_desc *group_desc = get_block(new_dev, 2);
  entry->bmap = group_desc->bg_block_bitmap;
  entry->imap = group_desc->bg_inode_bitmap;
  entry->iblock = group_desc->bg_inode_table;

  strcpy(entry->mount_name, path);
  strcpy(entry->name, disk);

  entry->inode_tbl_blk = group_desc->bg_inode_table;
  entry->inode_tbl_size = entry->ninodes * sizeof(struct ext2_inode);

  struct ext2_inode *inode_tbl = malloc(entry->inode_tbl_size);
  pread(new_dev, inode_tbl, entry->inode_tbl_size,
        entry->inode_tbl_blk * BLKSIZE);

  entry->inode_tbl = inode_tbl;

  mip->mounted = 1;
  mip->parent_mount = mip->mptr->dev;  // used to traverse up out of the mount
  mip->mptr = entry;

  return 0;
}

void write_inode_tbl(struct mntable *entry) {
  pwrite(entry->dev, entry->inode_tbl, entry->inode_tbl_size,
         entry->inode_tbl_blk * BLKSIZE);
  iput(entry->mounted_inode);
  sync();
}

int umount(char *path) {
  struct mntable *entry = mount_tbl;
  while (entry->dev != 0 && (entry - mount_tbl) < MOUNT_TBL_SIZE) {
    if (strcmp(path, entry->mount_name) == 0 && !entry->busy) {
      entry->mounted_inode->mounted = 0;
      write_inode_tbl(entry);
      close(entry->dev);
      entry->dev = 0;
      iput(entry->mounted_inode);
      printf("unmounted: %s\n", entry->mount_name);
      sync();
      return 0;
    }
    entry++;
  }

  return -1;
}

struct mntable *dev_to_mnt_entry(int dev) {
  for (int i = 0; i < MOUNT_TBL_SIZE; i++) {
    if (mount_tbl[i].dev == dev) {
      return &mount_tbl[i];
    }
  }
  return NULL;
}

void write_mnt_entries(void) {
  for (int i = 0; i < MOUNT_TBL_SIZE; i++) {
    if (mount_tbl[i].dev != 0) {
      write_inode_tbl(&mount_tbl[i]);
      close(mount_tbl[i].dev);
    }
  }
}