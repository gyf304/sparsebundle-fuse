/*
  sparsebundle: read-write, sparsebundle compatible fuse fs.
  Yifan Gu <me@yifangu.com>
  This program can be distributed under the terms of the GNU GPLv2.
*/

#define _GNU_SOURCE
#include <stdio.h>
#undef _GNU_SOURCE

#define _POSIX_C_SOURCE 200809L

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <pthread.h>

#include <yxml.h>
#include <uthash.h>
#include <utlist.h>
#include <utstring.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define XML_BUFFER_SIZE (1 << 10)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define LOG_ERROR(x) (fprintf(stderr, "sparsebundle: %s\n", x))
#define LOG_ERROR_FMT(f, ...) (fprintf(stderr, "sparsebundle: " f "\n", __VA_ARGS__))

static struct sparse_options {
	const char *path;
	const char *filename;
	int max_open_bands;
} sparse_options = {0};

#define OPTION(t, p) \
    { t, offsetof(struct sparse_options, p), 1 }

static const struct fuse_opt option_spec[] = {
	OPTION("--name=%s", filename),
	OPTION("--max-open-bands=%d", max_open_bands),
	FUSE_OPT_END
};

#define DEFAULT_FILENAME "sparsebundle.dmg"
#define DEFAULT_MAX_OPEN_BANDS 16

static struct sparse_info {
	int band_size;
	int size;
	int bundle_backingstore_version;
} sparse_info = {0};

struct sparse_band {
	int index;
	/* either fd, or negative errno */
	int fd;
	int write;
	pthread_rwlock_t rwlock;
	UT_hash_handle hh;
	struct sparse_band *prev;
	struct sparse_band *next;
};

static struct sparse_state {
	struct stat path_stat;
	struct {
		struct sparse_band *bands_dl;
		struct sparse_band *bands_ht;
		pthread_mutex_t lock;
	} lru;
} sparse_state = {0};

/* errno embedding version of posix functions */
inline static int eopen(const char *path, int flags, int perm) {
	int fd = open(path, flags, perm);
	return fd >= 0 ? fd : -errno;
}

inline static int epread(int fd, void *buf, size_t count, off_t offset)
{
	if (fd < 0) {
		return fd;
	}
	int r = pread(fd, buf, count, offset);
	return r >= 0 ? r : -errno;
}

inline static int epwrite(int fd, void *buf, size_t count, off_t offset)
{
	if (fd < 0) {
		return fd;
	}
	int r = pwrite(fd, buf, count, offset);
	return r >= 0 ? r : -errno;
}

/* locking lru.lock required */
inline static struct sparse_band *sparse_open_band(int id, int write)
{
	/* initialize band */
	struct sparse_band *band = calloc(1, sizeof(*band));
	band->index = id;
	char *path = NULL;
	asprintf(&path, "%s/bands/%x", sparse_options.path, id);
	band->fd = eopen(path, O_RDWR | (write ? O_CREAT : 0), sparse_state.path_stat.st_mode & 0666);
	free(path);
	band->write = write;
	pthread_rwlock_init(&band->rwlock, NULL);
	HASH_ADD_INT(sparse_state.lru.bands_ht, index, band);
	DL_APPEND(sparse_state.lru.bands_dl, band);
	return band;
}

/* locking lru.lock required */
inline static int sparse_close_band(struct sparse_band *band)
{
	int r = 0;
	HASH_DEL(sparse_state.lru.bands_ht, band);
	DL_DELETE(sparse_state.lru.bands_dl, band);
	/* wait for operation to complete on the band */
	pthread_rwlock_wrlock(&band->rwlock);
	pthread_rwlock_unlock(&band->rwlock);
	if (band->fd >= 0) {
		r = close(band->fd);
	}
	free(band);
	return r;
}

/* locking lru.lock required */
inline static int sparse_open_bands_count()
{
	return HASH_COUNT(sparse_state.lru.bands_ht);
}

inline static int sparse_close_bands()
{
	int r = 0;
	pthread_mutex_lock(&sparse_state.lru.lock);
	while (sparse_open_bands_count() > 0) {
		r = sparse_close_band(sparse_state.lru.bands_dl);
		if (r < 0) {
			break;
		}
	}
	pthread_mutex_unlock(&sparse_state.lru.lock);
	return r;
}

inline static struct sparse_band * sparse_get_band(int id, int write)
{
	struct sparse_band *band = NULL;
	pthread_mutex_lock(&sparse_state.lru.lock);
	HASH_FIND_INT(sparse_state.lru.bands_ht, &id, band);
	if (band != NULL) {
		/* band obtained */
		if (write > band->write) {
			/* reopen band for writing */
			sparse_close_band(band);
			band = sparse_open_band(id, write);
		} else {
			DL_DELETE(sparse_state.lru.bands_dl, band);
			DL_APPEND(sparse_state.lru.bands_dl, band);
		}
	} else {
		/* bind not found, time to open new bind. */
		/* close band if length exceeded */
		if (sparse_open_bands_count() >= sparse_options.max_open_bands) {
			sparse_close_band(sparse_state.lru.bands_dl);
		}
		band = sparse_open_band(id, write);	
	}
	pthread_rwlock_rdlock(&band->rwlock);
	pthread_mutex_unlock(&sparse_state.lru.lock);
	return band;
}

inline static void sparse_release_band(struct sparse_band *band)
{
	assert(band != NULL);
	pthread_rwlock_unlock(&band->rwlock);
}

static int sparse_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;
	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | (0555 & sparse_state.path_stat.st_mode);
		stbuf->st_atime = sparse_state.path_stat.st_atime;
		stbuf->st_mtime = sparse_state.path_stat.st_mtime;
		stbuf->st_ctime = sparse_state.path_stat.st_ctime;
		stbuf->st_uid = sparse_state.path_stat.st_uid;
		stbuf->st_gid = sparse_state.path_stat.st_gid;
		stbuf->st_nlink = 2;
	} else if (strcmp(path+1, sparse_options.filename) == 0) {
		stbuf->st_mode = S_IFREG | (0666 & sparse_state.path_stat.st_mode);
		stbuf->st_atime = sparse_state.path_stat.st_atime;
		stbuf->st_mtime = sparse_state.path_stat.st_mtime;
		stbuf->st_ctime = sparse_state.path_stat.st_ctime;
		stbuf->st_uid = sparse_state.path_stat.st_uid;
		stbuf->st_gid = sparse_state.path_stat.st_gid;
		stbuf->st_nlink = 1;
		stbuf->st_size = sparse_info.size;
	} else {
		res = -ENOENT;
	}
	return res;
}

static int sparse_readdir(
	const char *path, void *buf, fuse_fill_dir_t filler,
	off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;

	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	filler(buf, sparse_options.filename, NULL, 0);

	return 0;
}

static int sparse_open(const char *path, struct fuse_file_info *fi)
{
	if (strcmp(path+1, sparse_options.filename) != 0)
		return -ENOENT;

	if (fi->flags & O_CREAT || fi->flags & O_TRUNC)
		return -EACCES;

	return 0;
}

static int sparse_rw(void *buf, size_t count, off_t offset, int write)
{
	int acc = 0;
	int r = 0;
	int band_index;
	ssize_t band_offset, band_count;
	struct sparse_band *band = NULL;
	while (1) {
		if (count == 0) {
			break;
		}
		band_index = offset / sparse_info.band_size;
		band_offset = MIN(offset % sparse_info.band_size, sparse_info.band_size);
		band_count = MIN(sparse_info.band_size-band_offset, count);
		band = sparse_get_band(band_index, write);
		if (write) {
			r = epwrite(band->fd, buf+acc, band_count, band_offset);
		} else {
			r = epread(band->fd, buf+acc, band_count, band_offset);
			if (r == 0 || r == -ENOENT) {
				memset(buf+acc, 0, band_count);
				r = band_count;
			}
		}
		sparse_release_band(band);
		if (r < 0) {
			return r;
		}
		acc += r;
		count -= r;
		offset += r;
	}
	return acc;
}

static int sparse_read(
	const char *path, char *buf,
	size_t size, off_t offset,
	struct fuse_file_info *fi
)
{
	(void) fi;
	if (strcmp(path+1, sparse_options.filename) != 0)
		return -ENOENT;

	return sparse_rw((void *)buf, size, offset, 0);
}

static int sparse_write(
	const char *path, const char *buf,
	size_t size, off_t offset,
	struct fuse_file_info *fi
)
{
	(void) fi;
	if (strcmp(path+1, sparse_options.filename) != 0)
		return -ENOENT;

	return sparse_rw((void *)buf, size, offset, 1);
}

static int sparse_flush(const char *path, struct fuse_file_info *fi)
{
	(void) fi;
	if (strcmp(path+1, sparse_options.filename) != 0)
		return -ENOENT;

	return sparse_close_bands();
}

static struct fuse_operations sparse_oper = {
	.getattr	= sparse_getattr,
	.readdir	= sparse_readdir,
	.open		= sparse_open,
	.read		= sparse_read,
	.write		= sparse_write,
	.flush		= sparse_flush,
};

static int sparse_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
	switch (key) {
		case FUSE_OPT_KEY_NONOPT:
			if (!sparse_options.path) {
				sparse_options.path = arg;
				return 0;
			}
			break;
	}
	return 1;
}

static int sparse_parse_info_plist(yxml_t *parser, FILE* f)
{
	UT_string *cur_key = NULL;
	UT_string *cur_value = NULL;
	int in_key = 0;
	int in_value = 0;
	int ret = 0;
	const char *dict_path[] = {"plist", "dict"};
	int depth = 0;
	int match = 1;
	char c;
	while (fread(&c, 1, 1, f)) {
		yxml_ret_t r = yxml_parse(parser, c);
		if (r < 0) {
			LOG_ERROR("error while parsing plist");
			ret = 1;
			break;
		}
		switch (r) {
			case YXML_ELEMSTART:
				match = match &&
					(depth >= ARRAY_SIZE(dict_path) || strcmp(dict_path[depth], parser->elem) == 0);
				if (match && depth == ARRAY_SIZE(dict_path)) {
					if (strcmp(parser->elem, "key") == 0) {
						utstring_renew(cur_key);
						in_key = 1;
					} else {
						utstring_renew(cur_value);
						in_value = 1;
					}
				}
				depth++;
				break;
			case YXML_ELEMEND:
				if (in_value) {
					if (strcmp(utstring_body(cur_key), "band-size") == 0) {
						sparse_info.band_size = atoi(utstring_body(cur_value));
					} else if (strcmp(utstring_body(cur_key), "size") == 0) {
						sparse_info.size = atoi(utstring_body(cur_value));
					} else if (strcmp(utstring_body(cur_key), "bundle-backingstore-version") == 0) {
						sparse_info.bundle_backingstore_version = atoi(utstring_body(cur_value));
					}
				}
				in_key = 0;
				in_value = 0;
				depth--;
				if (depth == 0) {
					match = 1;
				}
				break;
			case YXML_CONTENT:
				if (in_value) {
					utstring_bincpy(cur_value, parser->data, strlen(parser->data));
				} else if (in_key) {
					utstring_bincpy(cur_key, parser->data, strlen(parser->data));
				}
				break;
			default:
				break;
		}
	}
	if (cur_key) {
		utstring_free(cur_key);
	}
	if (cur_value) {
		utstring_free(cur_value);
	}
	if (sparse_info.bundle_backingstore_version != 1) {
		LOG_ERROR("unsupported bundle-backingstore-version");
		ret = 1;
	}
	if (sparse_info.band_size <= 0) {
		LOG_ERROR("unable to obtain a valid band-size");
		ret = 1;
	}
	if (sparse_info.size <= 0) {
		LOG_ERROR("unable to obtain a valid size");
		ret = 1;
	}
	return ret;
}

static int sparse_init()
{
	sparse_options.max_open_bands = MAX(sparse_options.max_open_bands, 1);
	if (sparse_options.path == NULL) {
		LOG_ERROR("invalid path");
		return 1;
	}

	if (stat(sparse_options.path, &sparse_state.path_stat)) {
		LOG_ERROR(strerror(errno));
		return 1;
	}

	const char *plist_name = "Info.plist";
	char *plist_path = NULL;
	(void) asprintf(&plist_path, "%s/%s", sparse_options.path, plist_name);
	FILE* plist_file = fopen(plist_path, "r");
	if (plist_file == NULL) {
		free(plist_path);
		LOG_ERROR_FMT("unable to open %s", plist_name);
		return 1;
	}

	yxml_t *yxml_parser = malloc(sizeof(yxml_t) + XML_BUFFER_SIZE);;
	yxml_init(yxml_parser, yxml_parser+1, XML_BUFFER_SIZE);
	int plist_ret = sparse_parse_info_plist(yxml_parser, plist_file);
	fclose(plist_file);
	free(yxml_parser);
	free(plist_path);
	if (plist_ret) {
		return 1;
	}

	if (strstr(sparse_options.filename, "/") ||
		strcmp(sparse_options.filename, ".") == 0 ||
		strcmp(sparse_options.filename, "..") == 0) {
		LOG_ERROR("invalid filename");
		return 1;
	}

	pthread_mutex_init(&sparse_state.lru.lock, NULL);

	return 0;
}

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	sparse_options.filename = strdup(DEFAULT_FILENAME);
	sparse_options.max_open_bands = DEFAULT_MAX_OPEN_BANDS;
	if (fuse_opt_parse(&args, &sparse_options, option_spec, sparse_opt_proc) == -1) {
		return 1;
	}

	if (sparse_init()) {
		return 1;
	}

	return fuse_main(args.argc, args.argv, &sparse_oper, NULL);
}
