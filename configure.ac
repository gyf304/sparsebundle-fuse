AC_INIT([sparse], [1.0])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([-Wall -Werror subdir-objects foreign])
AC_PROG_CC
AM_PROG_AR
AC_PROG_LIBTOOL
AC_SUBST(LIBTOOL_DEPS)
AC_LTDL_DLLIB

AC_CHECK_HEADERS(windows.h)
AC_CHECK_FUNCS(pread)
AC_CHECK_FUNCS(pwrite)

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
