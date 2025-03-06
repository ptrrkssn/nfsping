AC_PREREQ([2.71])
AC_INIT([nfsping],[1.0.0],[pen@lysator.liu.se],[nfsping],[https://github.com/ptrrkssn/nfsping])
AC_CONFIG_AUX_DIR([build-aux])

AC_CONFIG_SRCDIR([nfsping.c])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
AC_PROG_LN_S
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Checks for header files.
dnl AC_CHECK_HEADERS([arpa/inet.h stdint.h])

# Checks for typedefs, structures, and compiler characteristics.
dnl AC_TYPE_SIZE_T

# Checks for library functions.
dnl AC_FUNC_ERROR_AT_LINE

dnl AC_CHECK_FUNCS([strdup strerror])

AC_SEARCH_LIBS([clnt_vc_create], [tirpc rpcsvc])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
