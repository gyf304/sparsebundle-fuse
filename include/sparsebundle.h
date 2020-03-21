#ifndef SPARSE_H
#define SPARSE_H

#include <stdio.h>
#include <stddef.h>

struct sparse_options {
	const char *path;
	int max_open_bands;
};

struct sparse_state;
typedef struct sparse_state *sparse_handle_t;

int sparse_pread(sparse_handle_t state, char *buf, size_t size, off_t offset);
int sparse_pwrite(sparse_handle_t state, const char *buf, size_t size, off_t offset);
int sparse_flush(sparse_handle_t state);
int sparse_trim(struct sparse_state *state, size_t size, off_t offset);

size_t sparse_get_size(sparse_handle_t state);
const char *sparse_get_error(sparse_handle_t state);
int sparse_open(sparse_handle_t *state_ptr, const struct sparse_options *options);
int sparse_close(sparse_handle_t *state_ptr);

#endif
