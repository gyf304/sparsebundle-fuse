nbd_plugindir = $(libdir)/nbdkit/plugins

nbd_plugin_LTLIBRARIES = nbdkit-sparsebundle-plugin.la
nbdkit_sparsebundle_plugin_la_SOURCES = plugin.c
nbdkit_sparsebundle_plugin_la_CFLAGS = -I$(top_srcdir)/include
nbdkit_sparsebundle_plugin_la_LDFLAGS = -module -shared
nbdkit_sparsebundle_plugin_la_LIBADD = \
	$(top_builddir)/sparsebundle/sparsebundle.la \
    $(NULL)
