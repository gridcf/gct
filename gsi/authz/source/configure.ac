AC_PREREQ([2.60])

AC_INIT([globus_authz],[4.3],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [4])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-callout >= 2, globus-gssapi-gsi >= 9, globus-authz-callout-error >= 2, globus-gsi-sysconfig >= 5"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

AM_CONDITIONAL([CYGPATH_W_DEFINED], [test "$CYGPATH_W" != ""])
PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_PATH_PROGS([OPENSSL], openssl)
AM_CONDITIONAL([ENABLE_TESTS], [test "x$OPENSSL" != x])

AC_PATH_PROGS([DOXYGEN], doxygen)
AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])

AC_CONFIG_FILES(
        globus-authz-uninstalled.pc
        globus-authz.pc
        Makefile
        Doxyfile
	version.h
        test/Makefile
        test/testcred.cnf)
AC_OUTPUT
