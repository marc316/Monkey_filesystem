/*-*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Monkey File System
 *  ------------------
 *  Copyright (C) 2012, Eduardo Silva P. <edsiper@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#define FUSE_USE_VERSION 26

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <unistd.h>
#include <dirent.h>

#include "mkfs.h"
#include "crc64.h"
#include "list.h"

#define STAT_CACHE_SIZE    100

struct stat stat_cache[STAT_CACHE_SIZE];

static int mkfs_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;
    char buf[MKFS_MAX_PATH];

	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else {
        /* FIXME: Cache must be implemented here */
        strncpy(buf, opt_root, root_size);
        strncpy(buf + root_size, path, strlen(path));
        buf[root_size + strlen(path)] = '\0';
        lstat(buf, stbuf);
    }

	return res;
}

static int mkfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;
    DIR *dir;
    struct dirent *ent;
    int len;
    char p[MKFS_MAX_PATH];

    /* Remap real path */
    len = strlen(path);
    strncpy(p, opt_root, root_size);
    strncpy(p + root_size, path, len);
    p[len + root_size] = 0;

    dir = opendir(p);
    if (!dir) {
        return -ENOENT;
    }

    while ((ent = readdir(dir)) != NULL) {
        filler(buf, ent->d_name, NULL, 0);
    }

    closedir(dir);

	return 0;
}

static int mkfs_open(const char *path, struct fuse_file_info *fi)
{
    int fd;
    int len;
    char p[MKFS_MAX_PATH];
    struct open_fd *ofd;

    /* Remap real path */
    len = strlen(path);
    strncpy(p, opt_root, root_size);
    strncpy(p + root_size, path, len);
    p[len + root_size] = 0;

    /* open flags, we just allow read-only mode */
	if ((fi->flags & 3) != O_RDONLY) {
		return -EACCES;
    }

    fd = open(p, O_RDONLY);
    if (fd < 0) {
        return -ENOENT;
    }

    /* Register path into the open fds list */
    ofd = malloc(sizeof(struct open_fd));
    ofd->hash = crc64(0, (const unsigned char *) path, len);
    ofd->fd   = fd;
    ofd->fi   = fi;
    fi->fh    = fd;
    mk_list_add(&ofd->_head, &open_fds_list);

	return 0;
}

static int mkfs_release(const char *path, struct fuse_file_info *fi)
{
    struct mk_list *head, *tmp;
    struct open_fd *ofd;
    uint64_t hash;

    /* get file hash */
    hash = crc64(0, (const unsigned char *) path, strlen(path));

    /* lookup entry */
    mk_list_foreach_safe(head, tmp, &open_fds_list) {
        ofd = mk_list_entry(head, struct open_fd, _head);
        if (ofd->hash == hash && ofd->fd == fi->fh) {
            mk_list_del(&ofd->_head);
            close(ofd->fd);
            free(ofd);
            break;
        }
    }

    return 0;
}

static int mkfs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	(void) fi;
    struct mk_list *head;
    struct open_fd *ofd;
    uint64_t hash;

    /* Lookup file descriptor */
    hash = crc64(0, (const unsigned char *) path, strlen(path));
    mk_list_foreach(head, &open_fds_list) {
        ofd = mk_list_entry(head, struct open_fd, _head);
        if (ofd->hash == hash && ofd->fd == fi->fh) {
            break;
        }
        ofd = NULL;
    }

    if (!ofd) {
        return -EBADF;
    }

    if (offset >= 0) {
        lseek(ofd->fd, offset, SEEK_SET);
    }

    return read(ofd->fd, buf, size);
}

int mkfs_umount(const char *path)
{
    return umount(path);
}

void mkfs_help(int rc)
{
    printf("Usage: mkfs [-d] -r root_directory  mount_point\n\n");
    printf("%sAvailable options%s\n", ANSI_BOLD, ANSI_RESET);
    printf("  -r, --root=DIR\tspecify root directory to be mounted\n");
    printf("  -d, --debug\t\tenable Fuse debug mode\n");
    printf("  -u, --umount\t\tumount a file system\n");
    printf("  -v, --version\t\tshow version number\n");
    printf("  -h, --help\t\tprint this help\n\n");
    exit(rc);
}

void mkfs_version()
{
    printf("%sMonkey Filesystem v0.1\n%s", ANSI_BOLD, ANSI_RESET);
    printf("Built with: GCC %i.%i.%i\n\n",
           __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
    fflush(stdout);
}

void mkfs_init()
{
    root_size = strlen(opt_root);
    mk_list_init(&open_fds_list);

}

static struct fuse_operations mkfs_oper = {
	.getattr	= mkfs_getattr,
	.readdir	= mkfs_readdir,
	.open		= mkfs_open,
    .release    = mkfs_release,
	.read		= mkfs_read,
};

int main(int argc, char *argv[])
{
    int opt;
    int ret;
    struct stat stbuf;

    static const struct option long_opts[] = {
        { "root",    required_argument, NULL, 'r' },
        { "help",    no_argument, NULL, 'h' },
        { "debug",   no_argument, NULL, 'd'},
        { "umount",  required_argument, NULL, 'u'},
        { "version", no_argument, NULL, 'v' },
		{ NULL, 0, NULL, 0 }
	};

    /* Fuse arguments */
    struct fuse_args args = FUSE_ARGS_INIT(0, NULL);

    opt_root = NULL;
    opt_debug = 0;
    root_size = 0;

    while ((opt = getopt_long(argc, argv, "r:u:hdv", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'r':
            opt_root = strdup(optarg);
            break;
        case 'd':
            opt_debug = 1;
            break;
        case 'h':
            mkfs_version();
            mkfs_help(EXIT_SUCCESS);
        case 'u':
            ret = mkfs_umount(optarg);
            exit(ret);
        case 'v':
            mkfs_version();
            exit(EXIT_SUCCESS);
        }
    }

    /* We must have a root directory */
    if (!opt_root) {
        fprintf(stderr, "Error: root directory have not been specified\n\n");
        mkfs_help(EXIT_FAILURE);
    }

    /* Validate the root directory */
    memset(&stbuf, 0, sizeof(struct stat));
    ret = lstat(opt_root, &stbuf);
    if (ret != 0) {
        fprintf(stderr, "Error: invalid root directory\n\n");
        exit(EXIT_FAILURE);
    }

    /* Check that root directory is a real directory */
    if (!S_ISDIR(stbuf.st_mode)) {
        fprintf(stderr, "Error: root is not a directory\n\n");
        exit(EXIT_FAILURE);
    }

    /* Validate mount point directory */
    if (strcmp(opt_root, argv[argc - 1]) == 0) {
        fprintf(stderr, "Error: choose a valid mount point directory\n\n");
        exit(EXIT_FAILURE);
    }

    ret = lstat(argv[argc - 1], &stbuf);
    if (ret != 0) {
        fprintf(stderr, "Error: invalid target directory\n\n");
        exit(EXIT_FAILURE);
    }
    /* Check that root directory is a real directory */
    if (!S_ISDIR(stbuf.st_mode)) {
        fprintf(stderr, "Error: root is not a directory\n\n");
        exit(EXIT_FAILURE);
    }

    /* Prepare internals */
    mkfs_init();

    /* Compose FUSE arguments */
    fuse_opt_parse(&args, NULL, NULL, NULL);
    fuse_opt_add_arg(&args, argv[0]);
    if (opt_debug) {
        fuse_opt_add_arg(&args, "-d");
    }
    fuse_opt_add_arg(&args, argv[argc -1]);

    memset(stat_cache, 0, sizeof(stat_cache));
	return fuse_main(args.argc, args.argv, &mkfs_oper, NULL);
}
