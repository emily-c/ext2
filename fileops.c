#include <assert.h>
#include <ext2fs/ext2_fs.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "alloc.h"
#include "fileops.h"
#include "mount.h"
#include "fileio.h"
#include "type.h"
#include "util.h"

#define MAX_PATH_DEPTH 20

MINODE minode[NMINODE];
MINODE *root;
PROC proc[NPROC], *running;

extern struct mntable mount_tbl[8];

int path_start_dev(char *path) {
  if (path == NULL) {
    return running->cwd->dev;
  } else if (path[0] == '/') {
    return root->dev;
  }

  return running->cwd->dev;
}

int get_dir_parent(uint8_t blk[BLKSIZE]) {
  // get .. entry
  struct ext2_dir_entry_2 *parent_ino_de = (struct ext2_dir_entry_2 *)blk;
  parent_ino_de = (struct ext2_dir_entry_2 *)((uint8_t *)parent_ino_de +
                                              parent_ino_de->rec_len);
  return parent_ino_de->inode;
}

int tokenize_path_str(char *path_str, char **path) {
  int nfiles = 0;

  char *cur_path_file = strtok(path_str, "/");

  while (cur_path_file && nfiles < MAX_PATH_DEPTH) {
    path[nfiles++] = cur_path_file;
    cur_path_file = strtok(NULL, "/");
  }

  return nfiles < MAX_PATH_DEPTH ? nfiles : -1;
}

// search directory's inode dir_entries for filename
uint32_t search_dir(const char *fname, uint32_t dir_inode, int *dev) {
  static char entry_name[EXT2_NAME_LEN];

  struct mntable *me = dev_to_mnt_entry(*dev);

  if ((me->inode_tbl[dir_inode - 1].i_mode & EXT2_S_IFDIR) == 0) {
    return 0;
  }

  struct ext2_inode *inode = &me->inode_tbl[dir_inode - 1];

  struct ext2_dir_entry_2 *de = get_block(*dev, inode->i_block[0]),
                          *end = (struct ext2_dir_entry_2 *)((uint8_t *)de +
                                                             BLKSIZE);

  while (de != end) {
    memcpy(entry_name, de->name, de->name_len);
    entry_name[de->name_len] = '\0';

    if (strcmp(fname, entry_name) == 0) {
      return de->inode;
    }

    de = (struct ext2_dir_entry_2 *)((uint8_t *)de + de->rec_len);
  }

  return 0;
}

// return parent mount point and the ino of the parent dir ino that contains the
// mount point
MINODE *get_mount_parent(int mnt_dev, int *out_parent_ino) {
  static uint8_t blk[BLKSIZE];

  MINODE *mounted_inode = dev_to_mnt_entry(mnt_dev)->mounted_inode;
  get_block_buf(mounted_inode->parent_mount, mounted_inode->INODE.i_block[0],
                blk);

  *out_parent_ino = get_dir_parent(blk);
  return mounted_inode;
}

// return tokenized filepath's inode
uint32_t search_path(char **filepath, int nfiles, int start_root, int *dev) {
  int new_dev;
  if (nfiles == 0) return 2;

  uint32_t cur_inode = running->cwd->ino;
  if (start_root) {
    cur_inode = 2;
    *dev = root->dev;
  }

  uint32_t prev_inode = 0;
  for (int i = 0; i < nfiles; i++) {
    prev_inode = cur_inode;
    if ((cur_inode = search_dir(filepath[i], cur_inode, dev)) == 0) return 0;

    // search up into parent partition
    if (strncmp(filepath[i], "..", 2) == 0 && cur_inode == prev_inode &&
        *dev != root->dev) {
      int parent_ino;
      *dev = get_mount_parent(*dev, &parent_ino)->parent_mount;
      cur_inode = parent_ino;
    } else if ((new_dev = find_mnt_dev(
                    *dev, cur_inode))) {  // search down into child partition
      *dev = new_dev;
      cur_inode = 2;
    }
  }

  return cur_inode;
}

int getino(int *dev, char *path) {
  int start_root = path[0] == '/';

  if (strcmp(path, "/") == 0) {
    return root->ino;
  }
  if (strcmp(path, ".") == 0) {
    return running->cwd->ino;
  }

  char *path_tok[MAX_PATH_DEPTH] = {0};
  int nfiles = tokenize_path_str(path, path_tok);
  if (nfiles == -1) {
    fprintf(stderr, "error: path too deep\n");
    return 1;
  }

  int file_ino = search_path(path_tok, nfiles, start_root, dev);
  return file_ino;
}

MINODE *iget(int dev, int ino) {
  if (ino == 0) {
    return NULL;
  }

  int i;
  for (i = 0; i < NMINODE; i++) {
    if (minode[i].ino == ino && minode[i].dev == dev) {
      minode[i].refCount++;
      return &minode[i];
    }
  }
  int j = 0;
  if (i == NMINODE) {
    for (j = 0; j < NMINODE; j++) {
      if (minode[j].ino == 0) {
        break;
      }
    }
  }

  minode[j].mptr = dev_to_mnt_entry(dev);
  minode[j].INODE = minode[j].mptr->inode_tbl[ino - 1];
  minode[j].ino = ino;
  minode[j].refCount++;
  minode[j].dev = dev;

  return &minode[j];
}

void iput(MINODE *mip) {
  mip->refCount--;
  mip->mptr->inode_tbl[mip->ino - 1] = mip->INODE;
}

void mount_root(const char *fname) {
  struct mntable *mte = mount_tbl;

  int dev = open(fname, O_RDWR);
  if (dev == -1) {
    perror("open");
    exit(1);
  }
  struct ext2_super_block *super_block =
      (struct ext2_super_block *)get_block(dev, 1);
  if (super_block->s_magic != EXT2_SUPER_MAGIC) {
    err("not a valid ext2 filesystem");
    exit(EXIT_FAILURE);
  }

  // initalize first mount table entry to /
  mte->ninodes = super_block->s_inodes_count;
  mte->nblocks = super_block->s_blocks_count;
  mte->dev = dev;
  mte->busy = 1;

  strcpy(mte->name, fname);
  strcpy(mte->mount_name, "/");

  struct ext2_group_desc *group_desc = get_block(dev, 2);
  mte->bmap = group_desc->bg_block_bitmap;
  mte->imap = group_desc->bg_inode_bitmap;
  mte->iblock = group_desc->bg_inode_table;

  mte->inode_tbl_blk = group_desc->bg_inode_table;
  mte->inode_tbl_size = mte->ninodes * sizeof(struct ext2_inode);

  struct ext2_inode *inode_tbl = malloc(mte->inode_tbl_size);
  pread(dev, inode_tbl, mte->inode_tbl_size, mte->inode_tbl_blk * BLKSIZE);

  mte->inode_tbl = inode_tbl;
  root = iget(dev, 2);
  proc[0].cwd = iget(dev, 2);
  proc[1].cwd = iget(dev, 2);

  mte->mounted_inode = root;
  root->mptr = mte;
  // root->mounted = 1;
}

struct stat loc_stat(char *path) {
  struct stat s;

  int d = path_start_dev(path);
  int ino = getino(&d, path);

  // ino might be zero in case of error
  // most code checks to see if the ino is 0 from stat for
  // detecting if a file does not exist
  if (ino == 0) {
    s.st_ino = 0;
    return s;
  }

  MINODE *minode = iget(d, ino);

  s.st_ino = minode->ino;
  s.st_dev = minode->dev;
  s.st_mode = minode->INODE.i_mode;
  s.st_nlink = minode->INODE.i_links_count;
  s.st_uid = minode->INODE.i_uid;
  s.st_gid = minode->INODE.i_gid;
  s.st_size = minode->INODE.i_size;
  s.st_blksize = BLKSIZE;
  s.st_blocks = minode->INODE.i_blocks;

  s.st_atim.tv_sec = minode->INODE.i_atime;
  s.st_ctim.tv_sec = minode->INODE.i_ctime;
  s.st_mtim.tv_sec = minode->INODE.i_mtime;

  return s;
}

int enter_child(
    MINODE *parent, int ino, char *basename,
    uint8_t file_type) {  // there will be a rec_len overflow test case

  struct ext2_dir_entry_2 *new;
  char *dir_blk;
  int dir_blk_ino;
  uint16_t new_rec_size;

  int cur_block = 0;

  // find most recent block
  for (int i = 11; i >= 0; i--) {
    if (parent->INODE.i_block[i]) {
      cur_block = i;
      break;
    }
  }

  dir_blk_ino = parent->INODE.i_block[cur_block];
  dir_blk = get_block(parent->dev, dir_blk_ino);

  struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)dir_blk;
  struct ext2_dir_entry_2 *end =
      (struct ext2_dir_entry_2 *)((uint8_t *)de + BLKSIZE);

  struct ext2_dir_entry_2 *prev = NULL;

  while (de != end) {
    prev = de;
    de = (struct ext2_dir_entry_2 *)((uint8_t *)de + de->rec_len);
  }

  uint16_t insert_size = EXT2_DIR_REC_LEN(strlen(basename));
  uint16_t prev_rec_len = EXT2_DIR_REC_LEN(prev->name_len);

  // do we have to go to the next block?
  if (((char *)end - ((char *)prev + prev_rec_len)) < insert_size) {
    // puts("making new dir block!");

    dir_blk_ino = parent->INODE.i_block[++cur_block];
    if (dir_blk_ino == 0) {
      dir_blk_ino = balloc(parent->dev);
      parent->INODE.i_block[cur_block] = dir_blk_ino;
      iput(parent);
    }

    new = (struct ext2_dir_entry_2 *)get_block(parent->dev, dir_blk_ino);
    new_rec_size = BLKSIZE;
  } else {
    // find insertion offset
    uint16_t old_rec_len = prev->rec_len;

    prev->rec_len = prev_rec_len;

    new_rec_size = old_rec_len - prev->rec_len;
    new = (struct ext2_dir_entry_2 *)((uint8_t *)prev + prev->rec_len);
  }

  int name_len = strlen(basename);

  new->inode = ino;
  new->name_len = name_len;
  new->rec_len = new_rec_size;
  new->file_type = file_type;

  memcpy(new->name, basename, name_len);

  put_block(parent->dev, dir_blk_ino, dir_blk);

  return 1;
}

void kmkdir(MINODE *pmip, char *base_name) {
  int ino = ialloc(pmip->dev);
  int blk = balloc(pmip->dev);
  MINODE *mip = iget(pmip->dev, ino);

  time_t now = time(0);

  mip->INODE.i_mode = EXT2_S_IFDIR | EXT2_S_IRUSR | EXT2_S_IWUSR |
                      EXT2_S_IWUSR | EXT2_S_IRGRP | EXT2_S_IXGRP |
                      EXT2_S_IROTH | EXT2_S_IXOTH;
  mip->INODE.i_blocks = 2;
  mip->INODE.i_size = BLKSIZE;
  mip->INODE.i_uid = running->uid;
  mip->INODE.i_gid = running->gid;
  mip->INODE.i_links_count = 2;

  mip->INODE.i_atime = now;
  mip->INODE.i_ctime = now;
  mip->INODE.i_mtime = now;

  mip->INODE.i_block[0] = blk;
  mip->dirty = 1;
  iput(mip);

  uint32_t old_rec_len;

  char dir_blk_0[BLKSIZE];
  struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)dir_blk_0;
  // cur dir '.' entry
  de->inode = ino;
  de->name_len = 1;
  de->name[0] = '.';

  de->rec_len = 12;
  de->file_type = EXT2_FT_DIR;

  old_rec_len = de->rec_len;

  de = (struct ext2_dir_entry_2 *)((uint8_t *)de + de->rec_len);
  // parent dir '..' entry
  de->inode = pmip->ino;
  de->name_len = 2;
  de->name[0] = '.';
  de->name[1] = '.';
  de->file_type = EXT2_FT_DIR;
  de->rec_len = BLKSIZE - old_rec_len;

  put_block(mip->dev, blk, dir_blk_0);

  enter_child(pmip, ino, base_name, EXT2_FT_DIR);
}

void loc_mkdir(char *path) {
  int dev = path[0] == '/' ? root->dev : running->cwd->dev;

  char dir_name_buf[256];

  char dir_buf[256];
  char base_buf[256];
  strcpy(dir_buf, path);
  strcpy(base_buf, path);

  char *dir_name, *base_name;
  dir_name = dirname(dir_buf);
  base_name = basename(base_buf);

  strcpy(dir_name_buf, dir_name);
  struct stat s = loc_stat(dir_name_buf);
  if ((s.st_ino == 0) ||
      (s.st_mode & EXT2_S_IFDIR) !=
          EXT2_S_IFDIR) {  // dirname must exist and is a DIR
    err("cannot create dir");
    return;
  }

  int parent_inode = getino(&dev, dir_name);
  MINODE *pmip = iget(dev, parent_inode);

  pmip->INODE.i_links_count++;
  pmip->dirty = 1;

  if ((pmip->INODE.i_mode & EXT2_S_IFDIR) != EXT2_S_IFDIR)
    return;  // check pmip is a dir
  if (search_dir(base_name, pmip->ino, &dev))
    return;  // basename must not exist in the parent dir

  kmkdir(pmip, base_name);
  iput(pmip);
}

int dir_empty(MINODE *idir) {
  struct ext2_dir_entry_2 *de = get_block(idir->dev, idir->INODE.i_block[0]),
                          *end = (struct ext2_dir_entry_2 *)((uint8_t *)de +
                                                             BLKSIZE);
  int cnt = 0;
  while (de != end) {
    de = (struct ext2_dir_entry_2 *)((uint8_t *)de + de->rec_len);
    if (++cnt > 2) return 0;
  }
  return 1;
}

void rm_child(MINODE *parent, char *name) {
  char *blk = get_block(parent->dev, parent->INODE.i_block[0]);

  struct ext2_dir_entry_2 *de = (void *)blk,
                          *end = (void *)((uint8_t *)de + BLKSIZE), *prev;
  while (de != end) {
    prev = de;

    de = (struct ext2_dir_entry_2 *)((uint8_t *)de + de->rec_len);

    if (strncmp(de->name, name, de->name_len) == 0) {
      prev->rec_len += de->rec_len;
      put_block(parent->dev, parent->INODE.i_block[0], blk);
      return;
    }
  }
}

#define USER_DEL_DIR_PERM (EXT2_S_IWUSR | EXT2_S_IXUSR)
#define GROUP_DEL_DIR_PERM (EXT2_S_IWGRP | EXT2_S_IXGRP)
#define OTHER_DEL_DIR_PERM (EXT2_S_IWOTH | EXT2_S_IXOTH)

// check deletion permissions -- only used for rmdir and rm
int check_permissions(MINODE *mip) {
  return mip->INODE.i_uid == running->uid;

  // uint16_t mode = mip->INODE.i_mode;
  // int is_dir = mode & EXT2_S_IFDIR;

  // if (mip->INODE.i_uid == running->uid) {
  //   uint16_t perm = is_dir ? USER_DEL_DIR_PERM : EXT2_S_IWUSR;
  //   return (mode & perm) == perm;
  // }

  // if (mip->INODE.i_gid == running->gid) {
  //   uint16_t perm = is_dir ? GROUP_DEL_DIR_PERM : EXT2_S_IWGRP;
  //   return (mode & perm) == perm;
  // }

  // uint16_t perm = is_dir ? OTHER_DEL_DIR_PERM : EXT2_S_IWOTH;
  // return (mode & perm) == perm;
}

void loc_rmdir(char *path) {
  if (strcmp(path, ".") == 0) {
    err("cannot delete current directory");
    return;
  }

  int dev = path[0] == '/' ? root->dev : running->cwd->dev;

  char path_cpy[256];
  strcpy(path_cpy, path);

  int ino = getino(&dev, path_cpy);
  MINODE *mip = iget(dev, ino);

  char dir_buf[256];
  char base_buf[256];
  strcpy(dir_buf, path);
  strcpy(base_buf, path);

  char *dir_name, *base_name;
  dir_name = dirname(dir_buf);
  base_name = basename(base_buf);

  strcpy(path_cpy, path);
  struct stat s = loc_stat(path_cpy);
  if ((s.st_ino == 0) ||
      (s.st_mode & EXT2_S_IFDIR) !=
          EXT2_S_IFDIR) {  // dirname must exist and is a DIR
    err("cannot delete...");
    return;
  }

  if ((mip->INODE.i_mode & EXT2_S_IFDIR) != EXT2_S_IFDIR) return;

  if (!check_permissions(mip)) {
    err("insufficient permissions");
    return;
  }

  if (!dir_empty(mip)) {
    err("dir not empty");
    return;
  }

  int parent_inode = getino(&dev, dir_name);
  MINODE *pmip = iget(dev, parent_inode);

  rm_child(pmip, base_name);

  truncat(mip);
  idealloc(dev, ino);
  iput(mip);
  pmip->dirty = 1;
  iput(pmip);
}

void loc_creat(char *path) {
  char path_buf[256];
  strcpy(path_buf, path);

  int dev = path_start_dev(path);
  if (getino(&dev, path_buf) != 0) {
    err("file already exists");
    return;
  }

  int ino = ialloc(dev);
  MINODE *mip = iget(dev, ino);

  time_t now = time(0);
  mip->INODE.i_mode =
      EXT2_S_IFREG | EXT2_S_IRUSR | EXT2_S_IWUSR | EXT2_S_IRGRP | EXT2_S_IROTH;
  mip->INODE.i_blocks = 0;
  mip->INODE.i_size = 0;
  mip->INODE.i_uid = running->uid;
  mip->INODE.i_gid = running->gid;
  mip->INODE.i_links_count = 1;

  mip->INODE.i_uid = running->uid;
  mip->INODE.i_gid = running->gid;

  mip->INODE.i_atime = now;
  mip->INODE.i_ctime = now;
  mip->INODE.i_mtime = now;

  mip->INODE.i_block[0] = 0;
  mip->dirty = 1;
  iput(mip);

  char dir_buf[256];
  char base_buf[256];
  strcpy(dir_buf, path);
  strcpy(base_buf, path);

  char *dir_name, *base_name;
  dir_name = dirname(dir_buf);
  base_name = basename(base_buf);

  int parent_inode = getino(&dev, dir_name);
  MINODE *pmip = iget(dev, parent_inode);

  enter_child(pmip, ino, base_name, (uint8_t)EXT2_FT_REG_FILE);
}

static int find_dir_name(uint8_t *dir_blk, int inode, char *out_name) {
  struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)dir_blk;
  struct ext2_dir_entry_2 *end =
      (struct ext2_dir_entry_2 *)((uint8_t *)de + BLKSIZE);

  while (de != end) {
    if (de->inode == inode) {
      memcpy(out_name, de->name, de->name_len);
      return de->name_len;
    }

    de = (struct ext2_dir_entry_2 *)((uint8_t *)de + de->rec_len);
  }

  return 0;
}

static int pwd_rec(MINODE *mip, int ino_search, char *working_dir) {
  static uint8_t blk[BLKSIZE];

  if (mip->ino == 2 && mip->dev == root->dev) {
    *working_dir++ = '/';
    get_block_buf(mip->dev, mip->INODE.i_block[0], blk);
    return 1 + find_dir_name(blk, ino_search, working_dir);
  }

  get_block_buf(mip->dev, mip->INODE.i_block[0], blk);
  int parent_ino = get_dir_parent(blk);

  MINODE *pino;

  // when recursively traversing up the fs to / but cross mount pt
  if (parent_ino == 2 && mip->dev != root->dev) {
    int parent_mnt_ino;
    MINODE *mount_pt = get_mount_parent(mip->dev, &parent_mnt_ino);
    pino = iget(mount_pt->parent_mount, mount_pt->ino);
  } else {
    pino = iget(mip->dev, parent_ino);
  }

  int cur_name_len = pwd_rec(pino, mip->ino, working_dir);

  if (ino_search != 0) {
    working_dir += cur_name_len;
    *working_dir++ = '/';

    // traversing down searching for names and cross mount pt
    int dev = mip->dev;
    if (mip->mounted && (dev = find_mnt_dev(mip->dev, mip->ino))) {
      mip = iget(dev, 2);
    }

    get_block_buf(dev, mip->INODE.i_block[0], blk);
    return 1 + cur_name_len + find_dir_name(blk, ino_search, working_dir);
  }

  *(working_dir + cur_name_len) = '\0';
  return cur_name_len;
}

char *pwd(char *out_path) {
  if (running->cwd->ino == 2) {
    if (running->cwd == root) {
      strcpy(out_path, "/");
    } else {
      strcpy(out_path, running->cwd->mptr->mount_name);
    }
  } else {
    pwd_rec(running->cwd, 0, out_path);
  }

  return out_path;
}

void loc_link(char *old_name, char *new_name) {
  char old_name_buf[256];
  char new_name_buf[256];

  strcpy(old_name_buf, old_name);

  int dev = path_start_dev(old_name);
  int oino = getino(&dev, old_name_buf);
  MINODE *omip = iget(dev, oino);

  strcpy(old_name_buf, old_name);
  strcpy(new_name_buf, new_name);
  struct stat s = loc_stat(old_name_buf);
  if (s.st_ino == 0) {
    err("failed ino");
    return;
  } else if ((s.st_mode & EXT2_S_IFDIR) == EXT2_S_IFDIR) {
    err("failed dir");
    return;
  } else if (getino(&dev, new_name_buf) != 0) {
    err("already exists");
    return;
  }

  char dir_buf[256];
  char base_buf[256];
  strcpy(dir_buf, new_name);
  strcpy(base_buf, new_name);

  char *dir_name, *base_name;
  dir_name = dirname(dir_buf);
  base_name = basename(base_buf);

  int parent_inode = getino(&dev, dir_name);
  MINODE *pmip = iget(dev, parent_inode);

  enter_child(pmip, omip->ino, base_name, EXT2_FT_REG_FILE);
  omip->INODE.i_links_count++;
  omip->dirty = 1;
  iput(omip);
  iput(pmip);

  printf("created %s -> %s\n", old_name, new_name);
}

void loc_unlink(char *pathname) {
  char pathname_buf[256];

  strcpy(pathname_buf, pathname);

  int dev = path_start_dev(pathname);
  int ino = getino(&dev, pathname_buf);
  MINODE *mip = iget(dev, ino);

  strcpy(pathname_buf, pathname);
  struct stat s = loc_stat(pathname_buf);
  if (s.st_ino == 0) {
    err("does not exist");
    return;
  } else if ((s.st_mode & EXT2_S_IFREG) != EXT2_S_IFREG &&
             (s.st_mode & EXT2_S_IFLNK) != EXT2_S_IFLNK) {
    err("is NOT REG or SLINK");
    return;
  }

  char dir_buf[256];
  char base_buf[256];
  strcpy(dir_buf, pathname);
  strcpy(base_buf, pathname);

  char *dir_name, *base_name;
  dir_name = dirname(dir_buf);
  base_name = basename(base_buf);

  int parent_inode = getino(&dev, dir_name);
  MINODE *pmip = iget(dev, parent_inode);

  rm_child(pmip, base_name);
  pmip->dirty = 1;
  iput(pmip);

  mip->INODE.i_links_count--;
  if (mip->INODE.i_links_count > 0) {
    mip->dirty = 1;
    iput(mip);
  }

  if (!(mip->INODE.i_mode & EXT2_S_IFLNK)) {
    truncat(mip);
    idealloc(dev, ino);
    iput(mip);
  }
}

void loc_symlink(char *old_name, char *new_name) {
  char old_name_buf[256];
  char new_name_buf[256];

  strcpy(old_name_buf, old_name);

  int dev = path_start_dev(old_name);

  strcpy(old_name_buf, old_name);
  strcpy(new_name_buf, new_name);
  struct stat s = loc_stat(old_name_buf);
  if (s.st_ino == 0) {
    err("failed ino");
    return;
  } else if (getino(&dev, new_name_buf) != 0) {
    err("already exists");
    return;
  }

  char dir_buf[256];
  char base_buf[256];
  strcpy(dir_buf, new_name);
  strcpy(base_buf, new_name);

  char *dir_name, *base_name;
  dir_name = dirname(dir_buf);
  base_name = basename(base_buf);

  int parent_inode = getino(&dev, dir_name);
  MINODE *pmip = iget(dev, parent_inode);

  int ino = ialloc(dev);
  MINODE *mip = iget(dev, ino);

  time_t now = time(0);
  mip->INODE.i_mode =
      EXT2_S_IFLNK | EXT2_S_IRUSR | EXT2_S_IWUSR | EXT2_S_IRGRP | EXT2_S_IROTH;
  mip->INODE.i_blocks = 0;
  mip->INODE.i_size = 0;
  mip->INODE.i_uid = running->uid;
  mip->INODE.i_gid = running->gid;
  mip->INODE.i_links_count = 1;

  mip->INODE.i_atime = now;
  mip->INODE.i_ctime = now;
  mip->INODE.i_mtime = now;

  memset(mip->INODE.i_block, 0, 15 * sizeof(uint32_t));
  memcpy(mip->INODE.i_block, old_name, strlen(old_name));
  mip->dirty = 1;
  iput(mip);

  enter_child(pmip, ino, base_name, EXT2_FT_SYMLINK);
  pmip->dirty = 1;
  iput(pmip);
}

size_t loc_readlink(char *pathname, uint32_t buf[15]) {
  char pathname_buf[256] = {0};

  strcpy(pathname_buf, pathname);

  int dev = path_start_dev(pathname);
  int ino = getino(&dev, pathname_buf);
  MINODE *mip = iget(dev, ino);

  strcpy(pathname_buf, pathname);
  struct stat s = loc_stat(pathname_buf);
  if (s.st_ino == 0) {
    err("does not exist");
    return 0;
  } else if ((s.st_mode & EXT2_S_IFLNK) != EXT2_S_IFLNK) {
    err("is NOT SLINK");
    return 0;
  }

  memcpy(buf, mip->INODE.i_block, 15 * sizeof(uint32_t));
  return strlen((char *)buf);
}

void cd(char *path) {
  int dev = path_start_dev(path);
  int ino = getino(&dev, !path ? "/" : path);

  if (ino == 0) {
    err("directory does not exist");
    return;
  }
  MINODE *mi = iget(dev, ino);

  if ((mi->INODE.i_mode & EXT2_S_IFLNK) == EXT2_S_IFLNK) {
    uint32_t link_buf[15] = {0};
    loc_readlink(path, link_buf);
    int ino = getino(&mi->dev, (char *)link_buf);
    mi = iget(mi->dev, ino);
  } else if ((mi->INODE.i_mode & EXT2_S_IFDIR) != EXT2_S_IFDIR) {
    err("not a directory");
    return;
  }

  running->cwd = mi;
}

void ls_file(char *fname) {
  static char *t1 = "xwrxwrxwr-------";
  static char *t2 = "----------------";
  static char name_buf[256];

  strcpy(name_buf, fname);
  struct stat s = loc_stat(name_buf);
  struct stat *sp = &s;

  int i, is_dir = 0;
  char ftime[64] = {0};

  if ((sp->st_mode & 0xF000) == 0x8000) printf("%c", '-');
  if ((sp->st_mode & 0xF000) == 0x4000) {
    is_dir = 1;
    printf("%c", 'd');
  }
  if ((sp->st_mode & 0xF000) == 0xA000) printf("%c", 'l');

  for (i = 8; i >= 0; i--) {
    if (sp->st_mode & (1 << i))
      printf("%c", t1[i]);
    else
      printf("%c", t2[i]);
  }

  printf("%4d ", (int)sp->st_nlink);
  printf("%4d ", sp->st_gid);
  printf("%4d ", sp->st_uid);
  printf("%8d ", (int)sp->st_size);

  // print time
  strcpy(ftime, ctime(&sp->st_mtim.tv_sec));
  ftime[strlen(ftime) - 1] = 0;
  printf("%s  ", ftime);

  // print name
  char *col = is_dir ? BLUE_COL : PURPLE_COL;
  printf("%s%s%s%s", col, basename(fname), REG_COL, (is_dir ? "/" : ""));
}

void ls(char *path) {
  char path_buf[256];
  char name[256];
  MINODE *minode;
  int dev = path_start_dev(path);
  int is_dir = 0;

  strcpy(path_buf, path);

  if (strcmp(path, "") == 0) {
    minode = running->cwd;
  } else {
    int ino = getino(&dev, path);
    if (ino == 0) {
      err("does not exist");
      return;
    }
    minode = iget(dev, ino);

    if ((minode->INODE.i_mode & EXT2_S_IFREG) == EXT2_S_IFREG ||
        (minode->INODE.i_mode & EXT2_S_IFLNK) == EXT2_S_IFLNK) {
      ls_file(path_buf);
      putchar('\n');
      return;
    }

    if ((minode->INODE.i_mode & EXT2_S_IFDIR) == EXT2_S_IFDIR) {
      is_dir = 1;
    }
  }

  struct ext2_dir_entry_2 *de = get_block(dev, minode->INODE.i_block[0]),
                          *end = (struct ext2_dir_entry_2 *)((uint8_t *)de +
                                                             BLKSIZE);

  static char full_path[256];

  while (de != end) {
    memcpy(name, de->name, de->name_len);
    name[de->name_len] = '\0';

    sprintf(full_path, "%s/%s", path_buf, name);
    if (is_dir) {
      ls_file(full_path);
    } else {
      ls_file(name);
    }

    MINODE *mi = iget(dev, de->inode);
    if ((mi->INODE.i_mode & EXT2_S_IFLNK) == EXT2_S_IFLNK) {
      uint32_t link_buf[15];
      loc_readlink(full_path, link_buf);
      printf(" -> %s%s%s", CYAN_COL, (char *)link_buf, REG_COL);
    }
    putchar('\n');
    de = (struct ext2_dir_entry_2 *)((uint8_t *)de + de->rec_len);
  }
}

void loc_rm(char *path) {
  int dev = path[0] == '/' ? root->dev : running->cwd->dev;

  char path_cpy[256];
  strcpy(path_cpy, path);

  int ino = getino(&dev, path_cpy);
  MINODE *mip = iget(dev, ino);

  char dir_buf[256];
  char base_buf[256];
  strcpy(dir_buf, path);
  strcpy(base_buf, path);

  char *dir_name, *base_name;
  dir_name = dirname(dir_buf);
  base_name = basename(base_buf);
  strcpy(path_cpy, path);
  struct stat s = loc_stat(path_cpy);
  if ((s.st_ino == 0) || (s.st_mode & EXT2_S_IFDIR) == EXT2_S_IFDIR) {
    err("Cannot delete...");
    return;
  }

  if (!check_permissions(mip)) {
    err("insufficient permissions");
    return;
  }

  if (s.st_mode & EXT2_S_IFLNK) {
    strcpy(path_cpy, path);
    loc_unlink(path_cpy);
    return;
  }

  int parent_inode = getino(&dev, dir_name);
  MINODE *pmip = iget(dev, parent_inode);

  if ((pmip->INODE.i_mode & EXT2_S_IFDIR) != EXT2_S_IFDIR)
    return;  // check pmip is a dir

  rm_child(pmip, base_name);

  truncat(mip);
  idealloc(dev, ino);
  iput(mip);
  pmip->INODE.i_links_count--;
  pmip->dirty = 1;
  iput(pmip);
}

void pfd(void) {
  for (int i = 0; i < NFD; i++) {
    if (running->fd[i] != NULL) {
      printf("%d\n", i);
    }
  }
}

void loc_chmod(char *mode, char *pathname) {
  int dev = path_start_dev(pathname);
  int ino = getino(&dev, pathname);
  MINODE *mip = iget(dev, ino);
  int newmode = 0;
  sscanf(mode, "%o", &newmode);
  mip->INODE.i_mode = (mip->INODE.i_mode & 0xF000) | (newmode & 0x0FFF);
  mip->dirty = 1;
  iput(mip);
}

void loc_touch(char *pathname) {
  int dev = path_start_dev(pathname);
  int ino = getino(&dev, pathname);
  if (ino == 0) {
    err("file does not exist");
    return;
  }
  MINODE *mip = iget(dev, ino);
  mip->INODE.i_mtime = time(0L);
  mip->dirty = 1;
  iput(mip);
}

void switch_proc(int proc_num) {
  if (proc_num >= NPROC) {
    err("proc not found");
  } else {
    running = &proc[proc_num];
  }
}

void list_proc(void) {
  for (int i = 0; i < NPROC; i++) {
    printf("proc %d %s%s%s\n", i, GREEN_COL, (running - proc) == i ? "<-" : "",
           REG_COL);
  }
}

void quit() {
  for (int i = 0; i < NMINODE; i++) {
    if (minode[i].ino != 0) iput(&minode[i]);
  }
  write_mnt_entries();
  sync();
  exit(0);
}

void init_procs(void) {
  for (int i = 0; i < NPROC; i++) {
    proc[i].uid = i;
    proc[i].pid = i;
    proc[i].gid = i;
    proc[i].cwd = root;
  }

  // make proc 0 and proc 1 have the same gid to test perms
  proc[1].gid = proc[0].gid;
}

void init(void) {
  running = &proc[0];

  signal(SIGINT, quit);
}
