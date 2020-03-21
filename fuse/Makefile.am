if WITH_FUSE
bin_PROGRAMS = sparsebundle-fuse
sparsebundle_fuse_SOURCES = main.c
sparsebundle_fuse_CFLAGS = \
	-I$(top_srcdir)/include \
	-D_FILE_OFFSET_BITS=64
sparsebundle_fuse_LDFLAGS = \
	-lfuse \
	$(NULL)
sparsebundle_fuse_LDADD = \
	$(top_builddir)/sparsebundle/libsparsebundle.la \
	$(NULL)
endif
