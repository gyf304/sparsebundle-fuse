/*
  sparsebundle: read-write, sparsebundle compatible fuse fs.
  Yifan Gu <me@yifangu.com>
  This program can be distributed under the terms of the GNU GPLv2.
*/

#define _POSIX_C_SOURCE 200809L
#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

#include "sparsebundle.h"

#define xstr(s) str(s)
#define str(s) #s

#define DEFAULT_MAX_OPEN_BANDS 16

static struct sparse_fuse_options {
    char *filename;
	int show_help;
    struct sparse_options options;
} sparse_fuse_options = {0};

static struct sparse_state *sparse_state = NULL;

#define OPTION(t, p) \
    { t, offsetof(struct sparse_fuse_options, p), 1 }

static const struct fuse_opt option_spec[] = {
	OPTION("--name=%s", filename),
	OPTION("--help", show_help),
	OPTION("--max-open-bands=%d", options.max_open_bands),
	FUSE_OPT_END
};

#define DEFAULT_FILENAME "sparsebundle.dmg"


static int sparse_fuse_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;
	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0555;
		stbuf->st_uid = getuid();
		stbuf->st_gid = getgid();
		stbuf->st_nlink = 2;
	} else if (strcmp(path+1, sparse_fuse_options.filename) == 0) {
		stbuf->st_mode = S_IFREG | 0666;
		stbuf->st_uid = getuid();
		stbuf->st_gid = getgid();
		stbuf->st_nlink = 1;
		stbuf->st_size = sparse_get_size(sparse_state);
	} else {
		res = -ENOENT;
	}
	return res;
}

static int sparse_fuse_readdir(
	const char *path, void *buf, fuse_fill_dir_t filler,
	off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;

	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	filler(buf, sparse_fuse_options.filename, NULL, 0);

	return 0;
}

static int sparse_fuse_open(const char *path, struct fuse_file_info *fi)
{
	if (fi->flags & O_CREAT || fi->flags & O_TRUNC)
		return -EACCES;

	if (strcmp(path+1, sparse_fuse_options.filename) != 0)
		return -ENOENT;

	return 0;
}

static int sparse_fuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    if (strcmp(path+1, sparse_fuse_options.filename) != 0)
		return -ENOENT;

    return sparse_pread(sparse_state, buf, size, offset);
}

static int sparse_fuse_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    if (strcmp(path+1, sparse_fuse_options.filename) != 0)
		return -ENOENT;

    return sparse_pwrite(sparse_state, buf, size, offset);
}

static int sparse_fuse_flush(const char *path, struct fuse_file_info *fi)
{
    if (strcmp(path+1, sparse_fuse_options.filename) != 0)
		return -ENOENT;

    return sparse_flush(sparse_state);
}

static struct fuse_operations sparse_oper = {
	.getattr	= sparse_fuse_getattr,
	.readdir	= sparse_fuse_readdir,
	.open		= sparse_fuse_open,
	.read		= sparse_fuse_read,
	.write		= sparse_fuse_write,
	.flush		= sparse_fuse_flush,
};

static int sparse_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
	switch (key) {
		case FUSE_OPT_KEY_NONOPT:
			if (!sparse_fuse_options.options.path) {
				sparse_fuse_options.options.path = arg;
				return 0;
			}
			break;
	}
	return 1;
}

static void usage(const char *progname)
{
	printf(
"usage: %s sparsebundle mountpoint [options]\n"
"\n"
"    -h   --help            print help\n"
"    -f                     foreground operation\n"
"    -s                     disable multi-threaded operation\n"
"    --max-open-bands=N     maximum band files open (default: " xstr(DEFAULT_MAX_OPEN_BANDS) ")\n", progname);
}

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	sparse_fuse_options.filename = strdup(DEFAULT_FILENAME);
	sparse_fuse_options.options.max_open_bands = DEFAULT_MAX_OPEN_BANDS;
	if (fuse_opt_parse(&args, &sparse_fuse_options, option_spec, sparse_opt_proc) == -1) {
		return 1;
	}

	if (sparse_fuse_options.show_help) {
		usage(argv[0]);
		return 0;
	}

	if (sparse_open(&sparse_state, &sparse_fuse_options.options)) {
        fprintf(stderr, "sparsebundle: %s\n", sparse_get_error(sparse_state));
		return 1;
	}

	return fuse_main(args.argc, args.argv, &sparse_oper, NULL);
}
