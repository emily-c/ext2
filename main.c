#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fileops.h"
#include "mount.h"
#include "fileio.h"
#include "util.h"

void print_help(void) {
  puts("commands:\n");
  puts(
      " cd ls pwd mkdir rmdir rm creat link unlink symlink\n"
      " readlink chmod touch open read write lseek close\n"
      " pfd cat cp mv mount umount cs help quit\n");
  puts("open modes: 0 - read, 1 - write, 2 - rw, 3 - append\n");
}

int main(int argc, char **argv) {
  if(argc < 2) {
    puts("usage: fs diskimage");
    return 1;
  }

  init();
  mount_root(argc > 1 ? argv[1] : "diskimage");
  init_procs();

  printf("%sType 'help' for a list of commands%s\n", YELLOW_COL, REG_COL);

  char path_buf[256];
  char line[1024], cmd[100], arg1[100], arg2[100];
  for (;;) {
    printf("%s%s%s $ ", GREEN_COL, pwd(path_buf), REG_COL);
    memset(line, 0, sizeof(line));
    memset(cmd, 0, sizeof(cmd));
    memset(arg1, 0, sizeof(arg1));
    memset(arg2, 0, sizeof(arg2));

    fgets(line, sizeof(line), stdin);
    line[strlen(line) - 1] = 0;

    sscanf(line, "%s %s %s", cmd, arg1, arg2);

    if (!strcmp(cmd, "help")) {
      print_help();
      continue;
    }

    if (!strcmp(cmd, "cd")) {
      cd(arg1);
    } else if (!strcmp(cmd, "ls")) {
      ls(arg1);
    } else if (!strcmp(cmd, "pwd")) {
      puts(path_buf);
    } else if (!strcmp(cmd, "mkdir")) {
      loc_mkdir(arg1);
    } else if (!strcmp(cmd, "rmdir")) {
      loc_rmdir(arg1);
    } else if (!strcmp(cmd, "rm")) {
      loc_rm(arg1);
    } else if (!strcmp(cmd, "creat")) {
      loc_creat(arg1);
    } else if (!strcmp(cmd, "link")) {
      loc_link(arg1, arg2);
    } else if (!strcmp(cmd, "unlink")) {
      loc_unlink(arg1);
    } else if (!strcmp(cmd, "symlink")) {
      loc_symlink(arg1, arg2);
    } else if (!strcmp(cmd, "readlink")) {
      uint32_t buf[15];
      if (loc_readlink(arg1, buf) > 0) puts((char *)buf);
    } else if (!strcmp(cmd, "chmod")) {
      loc_chmod(arg1, arg2);
    } else if (!strcmp(cmd, "touch")) {
      loc_touch(arg1);
    } else if (!strcmp(cmd, "open")) {
      char arg1_buf[255];
      strcpy(arg1_buf, arg1);

      int fd = loc_open(arg1, (enum open_flags)atoi(arg2));
      if (fd != -1)
        printf("opened %s with fd %d\n", arg1_buf, fd);
      else
        printf("error: too many files open\n");
    } else if (!strcmp(cmd, "close")) {
      loc_close(atoi(arg1));
    } else if (!strcmp(cmd, "pfd")) {
      pfd();
    } else if (!strcmp(cmd, "read")) {
      int nbytes = atoi(arg2);
      char *read_buf = calloc(1, nbytes + 100);
      loc_read(atoi(arg1), read_buf, nbytes);
      printf("%s", read_buf);
      free(read_buf);
    } else if (!strcmp(cmd, "write")) {
      loc_write(atoi(arg1), arg2, strlen(arg2));
    } else if (!strcmp(cmd, "lseek")) {
      loc_lseek(atoi(arg1), atoi(arg2));
    } else if (!strcmp(cmd, "cat")) {
      cat(arg1);
    } else if (!strcmp(cmd, "mv")) {
      mv(arg1, arg2);
    } else if (!strcmp(cmd, "cp")) {
      cp(arg1, arg2);
    } else if (!strcmp(cmd, "mount")) {
      if (*arg1 == '\0') {
        mount_list();
      } else {
        mount_fs(arg1, arg2);
      }
    } else if (!strcmp(cmd, "umount")) {
      if (umount(arg1) == -1) {
        puts("error: cannot umount");
      }
    } else if (!strcmp(cmd, "cs")) {
      if (*arg1 == '\0') {
        list_proc();
      } else {
        switch_proc(atoi(arg1));
      }
    } else if (!strcmp(cmd, "quit")) {
      quit();
    } else {
        printf("%scommand \"%s\" not found%s\n", RED_COL, cmd, REG_COL);
    }
  }

  return 0;
}
