#ifndef MOUNT_H
#define MOUNT_H

void mount_list(void);
int mount_fs(char *disk, char *path);
int umount(char *path);
struct mntable *dev_to_mnt_entry(int dev);
void write_mnt_entries(void);
int find_mnt_dev(int old_dev, int inode);

#endif