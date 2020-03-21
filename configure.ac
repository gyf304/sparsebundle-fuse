AC_INIT([sparse], [1.0])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE(foreign)
AC_PROG_CC
AM_PROG_AR
AC_PROG_LIBTOOL
AC_SUBST(LIBTOOL_DEPS)
AC_LTDL_DLLIB

AC_SYS_LARGEFILE

AC_CHECK_HEADERS(windows.h)
AC_CHECK_FUNCS(pread)
AC_CHECK_FUNCS(pwrite)

AC_ARG_WITH([fuse],
	[AS_HELP_STRING([--without-fuse], [disable fuse support])],
	[],
	[with_fuse=yes])
if test "x$with_fuse" = "xyes"
then
	AC_CHECK_LIB(fuse, main, [],
		[AC_MSG_ERROR([libfuse not found, consider disabling fuse support with --without-fuse])])
fi
AM_CONDITIONAL([WITH_FUSE], [test "x$with_fuse" = "xyes"])

AC_ARG_WITH([nbdkit-plugin],
	[AS_HELP_STRING([--without-nbdkit-plugin], [disable nbdkit support])],
	[],
	[with_nbdkit_plugin=yes])
if test "x$with_nbdkit_plugin" = "xyes"
then
	AC_CHECK_HEADERS(nbdkit-plugin.h, [],
		[AC_MSG_ERROR([nbdkit-plugin.h not found, consider disabling nbdkit support with --without-nbdkit-plugin])])
fi
AM_CONDITIONAL([WITH_NBDKIT_PLUGIN], [test "x$with_nbdkit_plugin" = "xyes"])

# Tells automake to create a Makefile
# See https://www.gnu.org/software/automake/manual/html_node/Requirements.html
AC_CONFIG_FILES([
	sparsebundle/Makefile
	fuse/Makefile
	nbdkit-plugin/Makefile
	Makefile
])

# Generate the output
AC_OUTPUT
