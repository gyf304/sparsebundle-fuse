#define NBDKIT_API_VERSION 2

#include <string.h>
#include <stdlib.h>
#include <nbdkit-plugin.h>

#include "sparsebundle.h"

#define DEFAULT_MAX_OPEN_BANDS 16

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

#if defined(_WIN32) || defined(__MINGW32__) || defined(__CYGWIN__) || defined(_MSC_VER)
#define WINDOWS_COMPAT
#endif

struct sparse_options sparse_options = {
	.path = NULL,
	.max_open_bands = DEFAULT_MAX_OPEN_BANDS
};

static int sparse_nbd_config(const char *key, const char *value)
{
	if (strcmp(key, "path") == 0) {
		if (sparse_options.path != NULL) {
			nbdkit_error("path can only be specified once");
			return 1;
		}
#if defined(WINDOWS_COMPAT)
		sparse_options.path = strdup(value);
#else
		sparse_options.path = nbdkit_realpath(value);
#endif
		nbdkit_debug("path is %s", sparse_options.path);
	} else if (strcmp(key, "max-open-bands")) {
		int b = atoi(value);
		if (b <= 0) {
			nbdkit_error("invalid max-open-bands");
			return 1;
		}
		sparse_options.max_open_bands = atoi(value);
	}
	return 0;
}

static int sparse_nbd_config_complete()
{
	if (sparse_options.path == NULL) {
		nbdkit_error("path not supplied");
		return 1;
	}
	return 0;
}

static void *sparse_nbd_open(int readonly)
{
	sparse_handle_t handle = NULL;
	int ret = sparse_open(&handle, &sparse_options);
	if (ret) {
		nbdkit_error("%s", sparse_get_error(handle));
	}
	return handle;
}

static int64_t sparse_nbd_get_size (void *handle)
{
	size_t size = sparse_get_size((sparse_handle_t) handle);
	nbdkit_debug("size is %llu", size);
	return size;
}

static int sparse_nbd_pread(void *handle, void *buf, uint32_t count, uint64_t offset, uint32_t flags)
{
	int r = sparse_pread((sparse_handle_t) handle, buf, count, offset);
	if (r < 0) {
		errno = -r;
		return -1;
	}
	return 0;
}

static int sparse_nbd_pwrite(void *handle, const void *buf, uint32_t count, uint64_t offset, uint32_t flags)
{
	int r = sparse_pwrite((sparse_handle_t) handle, buf, count, offset);
	if (r < 0) {
		errno = -r;
		return -1;
	}
	return 0;
}

static int sparse_nbd_flush(void *handle, uint32_t flags)
{
	int r = sparse_flush((sparse_handle_t) handle);
	if (r < 0) {
		errno = -r;
		return -1;
	}
	return 0;
}

static int sparse_nbd_trim(void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
	int r = sparse_trim((sparse_handle_t) handle, count, offset);
	if (r < 0) {
		errno = -r;
		return -1;
	}
	return 0;
}

static struct nbdkit_plugin plugin = {
	.name              = "sparsebundle",
	.config            = sparse_nbd_config,
	.config_complete   = sparse_nbd_config_complete,
	.open              = sparse_nbd_open,
	.get_size          = sparse_nbd_get_size,
	.pread             = sparse_nbd_pread,
	.pwrite            = sparse_nbd_pwrite,
	.flush             = sparse_nbd_flush,
	.trim              = sparse_nbd_trim
};

NBDKIT_REGISTER_PLUGIN(plugin)
