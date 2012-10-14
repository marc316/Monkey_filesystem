
#include "list.h"

#ifndef MKFS_H
#define MKFS_H

/* program arguments */
int   opt_debug;
char *opt_root;

/* string size (number of bytes) of the root directory (strlen(opt_root)) */
int root_size;

#define MKFS_MAX_PATH       1024

/* ANSI Colors */
#define ANSI_BOLD "\033[1m"
#define ANSI_CYAN "\033[36m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_RED "\033[31m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE "\033[34m"
#define ANSI_GREEN "\033[32m"
#define ANSI_WHITE "\033[37m"
#define ANSI_RESET "\033[0m"


/* Handle a list of open file descriptors */
struct mk_list open_fds_list;

struct open_fd {
  uint64_t hash;
  int fd;
  struct fuse_file_info *fi;

  struct mk_list _head;
};

#endif
