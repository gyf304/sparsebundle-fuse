/*
  sparsebundle: read-write, sparsebundle compatible fuse fs.
  Yifan Gu <me@yifangu.com>
  This program can be distributed under the terms of the GNU GPLv2.
*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <pthread.h>
#include <sys/stat.h>

#include <yxml.h>
#include <uthash.h>
#include <utlist.h>
#include <utstring.h>

#include "sparsebundle.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define XML_BUFFER_SIZE (1 << 10)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

struct sparse_band {
	int index;
	/* either fd, or negative errno */
	int fd;
	pthread_rwlock_t rwlock;
	UT_hash_handle hh;
	struct sparse_band *prev;
	struct sparse_band *next;
};

struct sparse_info {
	int band_size;
	int size;
	int bundle_backingstore_version;
};

struct sparse_state {
	struct sparse_options options;
	struct sparse_info info;
	struct {
		struct sparse_band *bands_dl;
		struct sparse_band *bands_ht;
		pthread_mutex_t lock;
	} lru;
	const char *error;
};


#ifdef HAVE_WINDOWS_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef HAVE_PREAD
ssize_t pread(int fd, void *buf, size_t count, long long offset)
{
	OVERLAPPED o = {0,0,0,0,0};
	HANDLE fh = (HANDLE)_get_osfhandle(fd);
	uint64_t off = offset;
	DWORD bytes;
	BOOL ret;

	if (fh == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}

	o.Offset = off & 0xffffffff;
	o.OffsetHigh = (off >> 32) & 0xffffffff;

	ret = ReadFile(fh, buf, (DWORD)count, &bytes, &o);
	if (!ret) {
		errno = EIO;
		return -1;
	}

	return (ssize_t)bytes;
}
#endif

#ifndef HAVE_PWRITE
ssize_t pwrite(int fd, const void *buf, size_t count, long long offset)
{
	OVERLAPPED o = {0,0,0,0,0};
	HANDLE fh = (HANDLE)_get_osfhandle(fd);
	uint64_t off = offset;
	DWORD bytes;
	BOOL ret;

	if (fh == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}

	o.Offset = off & 0xffffffff;
	o.OffsetHigh = (off >> 32) & 0xffffffff;

	ret = WriteFile(fh, buf, (DWORD)count, &bytes, &o);
	if (!ret) {
		errno = EIO;
		return -1;
	}

	return (ssize_t)bytes;
}
#endif
#endif

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
inline static struct sparse_band *sparse_open_band(struct sparse_state *state, int id, int create)
{
	/* initialize band */
	struct sparse_band *band = calloc(1, sizeof(*band));
	band->index = id;
	UT_string *path; utstring_new(path);
	utstring_printf(path, "%s/bands/%x", state->options.path, id);
	band->fd = eopen(utstring_body(path), O_RDWR | (create ? O_CREAT : 0), 0666);
	utstring_free(path);
	pthread_rwlock_init(&band->rwlock, NULL);
	HASH_ADD_INT(state->lru.bands_ht, index, band);
	DL_APPEND(state->lru.bands_dl, band);
	return band;
}

/* locking lru.lock required */
inline static int sparse_close_band(struct sparse_state *state, struct sparse_band *band)
{
	int r = 0;
	HASH_DEL(state->lru.bands_ht, band);
	DL_DELETE(state->lru.bands_dl, band);
	/* wait for operation to complete on the band */
	pthread_rwlock_wrlock(&band->rwlock);
	pthread_rwlock_unlock(&band->rwlock);
	pthread_rwlock_destroy(&band->rwlock);
	if (band->fd >= 0) {
		r = close(band->fd);
	}
	free(band);
	return r;
}

/* locking lru.lock required */
inline static int sparse_open_bands_count(struct sparse_state *state)
{
	return HASH_COUNT(state->lru.bands_ht);
}

inline static int sparse_close_bands(struct sparse_state *state)
{
	int r = 0;
	pthread_mutex_lock(&state->lru.lock);
	while (sparse_open_bands_count(state) > 0) {
		r = sparse_close_band(state, state->lru.bands_dl);
		if (r < 0) {
			break;
		}
	}
	pthread_mutex_unlock(&state->lru.lock);
	return r;
}

inline static struct sparse_band *sparse_get_band(struct sparse_state *state, int id, int create)
{
	struct sparse_band *band = NULL;
	pthread_mutex_lock(&state->lru.lock);
	HASH_FIND_INT(state->lru.bands_ht, &id, band);
	if (band != NULL) {
		/* band obtained */
		if (create && band->fd == -ENOENT) {
			/* attempt to create band if not created yet. */
			sparse_close_band(state, band);
			band = sparse_open_band(state, id, create);
		} else {
			DL_DELETE(state->lru.bands_dl, band);
			DL_APPEND(state->lru.bands_dl, band);
		}
	} else {
		/* bind not found, time to open new bind. */
		/* close band if length exceeded */
		if (sparse_open_bands_count(state) >= state->options.max_open_bands) {
			sparse_close_band(state, state->lru.bands_dl);
		}
		band = sparse_open_band(state, id, create);	
	}
	pthread_rwlock_rdlock(&band->rwlock);
	pthread_mutex_unlock(&state->lru.lock);
	return band;
}

inline static int sparse_clear_band(struct sparse_state *state, int id)
{
	int r = 0;
	struct sparse_band *band = NULL;
	pthread_mutex_lock(&state->lru.lock);
	HASH_FIND_INT(state->lru.bands_ht, &id, band);
	if (band == NULL) {
		/* bind not found, time to open new bind. */
		/* close band if length exceeded */
		if (sparse_open_bands_count(state) >= state->options.max_open_bands) {
			sparse_close_band(state, state->lru.bands_dl);
		}
		band = sparse_open_band(state, id, 0);
	}
	// removing the file
	if (band->fd >= 0) {
		if (close(band->fd)) {
			r = -errno;
		}
		if (!r) {
			band->fd = -ENOENT;
			UT_string *path; utstring_new(path);
			utstring_printf(path, "%s/bands/%x", state->options.path, id);
			if (unlink(utstring_body(path))) {
				r = -errno;
			}
			utstring_free(path);
		}
	}
	pthread_mutex_unlock(&state->lru.lock);
	return r;
}

inline static void sparse_release_band(struct sparse_state *state, struct sparse_band *band)
{
	assert(band != NULL);
	pthread_rwlock_unlock(&band->rwlock);
}

inline static int sparse_rw(struct sparse_state* state, void *buf, size_t count, off_t offset, int write)
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
		band_index = offset / state->info.band_size;
		band_offset = MIN(offset % state->info.band_size, state->info.band_size);
		band_count = MIN(state->info.band_size-band_offset, count);
		band = sparse_get_band(state, band_index, write);
		if (write) {
			r = epwrite(band->fd, buf+acc, band_count, band_offset);
		} else {
			r = epread(band->fd, buf+acc, band_count, band_offset);
			if (r == 0 || r == -ENOENT) {
				memset(buf+acc, 0, band_count);
				r = band_count;
			}
		}
		sparse_release_band(state, band);
		if (r < 0) {
			return r;
		}
		acc += r;
		count -= r;
		offset += r;
	}
	return acc;
}

int sparse_pread(struct sparse_state *state, char *buf, size_t size, off_t offset)
{
	return sparse_rw(state, (void *)buf, size, offset, 0);
}

int sparse_pwrite(struct sparse_state *state, const char *buf, size_t size, off_t offset)
{
	return sparse_rw(state, (void *)buf, size, offset, 1);
}

int sparse_trim(struct sparse_state *state, size_t size, off_t offset)
{
	int r = 0;
	int start_band = (offset + state->info.band_size - 1) / state->info.band_size;
	int end_band = (offset + size) / state->info.band_size;
	for (int i = start_band; i < end_band; i++) {
		r = sparse_clear_band(state, i);
		if (r < 0) {
			break;
		}
	}
	return r;
}

int sparse_flush(struct sparse_state *state)
{
	return sparse_close_bands(state);
}

size_t sparse_get_size(struct sparse_state* state) {
	return state->info.size;
}

const char *sparse_get_error(struct sparse_state* state) {
	return state->error;
}

static int sparse_parse_info_plist(struct sparse_state* state, yxml_t *parser, FILE* f)
{
	struct sparse_info* info = &state->info;
	UT_string *cur_key = NULL;
	UT_string *cur_value = NULL;
	int ret = 0;
	int in_key = 0, in_value = 0;
	int depth = 0, match = 1;
	const char *dict_path[] = {"plist", "dict"};
	char c;
	while (fread(&c, 1, 1, f)) {
		yxml_ret_t r = yxml_parse(parser, c);
		if (r < 0) {
			state->error = "error while parsing plist";
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
						info->band_size = atoi(utstring_body(cur_value));
					} else if (strcmp(utstring_body(cur_key), "size") == 0) {
						info->size = atoi(utstring_body(cur_value));
					} else if (strcmp(utstring_body(cur_key), "bundle-backingstore-version") == 0) {
						info->bundle_backingstore_version = atoi(utstring_body(cur_value));
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
	if (info->bundle_backingstore_version != 1) {
		state->error = "unsupported bundle-backingstore-version";
		ret = 1;
	}
	if (info->band_size <= 0) {
		state->error = "unable to obtain a valid band-size";
		ret = 1;
	}
	if (info->size <= 0) {
		state->error = "unable to obtain a valid size";
		ret = 1;
	}
	return ret;
}

int sparse_open(struct sparse_state **state_ptr, const struct sparse_options *options)
{
	struct sparse_state *state = calloc(1, sizeof(struct sparse_state));
	*state_ptr = state;

	memcpy(&state->options, options, sizeof(struct sparse_options));
	state->options.max_open_bands = MAX(state->options.max_open_bands, 1);
	if (state->options.path == NULL) {
		state->error = "invalid path";
		return 1;
	}

	struct stat bands_stat;
	UT_string *bands_path = NULL; utstring_new(bands_path);
	utstring_printf(bands_path, "%s/%s", state->options.path, "bands");
	if (stat(utstring_body(bands_path), &bands_stat)) {
		utstring_free(bands_path);
		state->error = "cannot stat bands";
		return 1;
	}
	utstring_free(bands_path);
	if (!(bands_stat.st_mode & S_IFDIR)) {
		state->error = "bands should be a directory";
		return 1;
	}

	UT_string *plist_path = NULL; utstring_new(plist_path);
	utstring_printf(plist_path, "%s/%s", state->options.path, "Info.plist");
	FILE* plist_file = fopen(utstring_body(plist_path), "r");
	if (plist_file == NULL) {
		utstring_free(plist_path);
		state->error = "unable to open Info.plist";
		return 1;
	}

	yxml_t *yxml_parser = malloc(sizeof(yxml_t) + XML_BUFFER_SIZE);;
	yxml_init(yxml_parser, yxml_parser+1, XML_BUFFER_SIZE);
	int plist_ret = sparse_parse_info_plist(state, yxml_parser, plist_file);
	fclose(plist_file);
	free(yxml_parser);
	utstring_free(plist_path);
	if (plist_ret) {
		return 1;
	}

	pthread_mutex_init(&state->lru.lock, NULL);

	return 0;
}

int sparse_close(struct sparse_state **state_ptr)
{
	struct sparse_state *state = *state_ptr;
	sparse_flush(state);
	free(state);
	*state_ptr = NULL;
	return 0;
}
