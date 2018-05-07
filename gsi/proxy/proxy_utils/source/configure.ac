AC_PREREQ([2.60])

AC_INIT([globus_proxy_utils],[7.1],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [2])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-openssl-module >= 3, globus-gsi-openssl-error >= 2, globus-gsi-cert-utils >= 8, globus-gsi-sysconfig >= 5, globus-gsi-credential >= 5, globus-gsi-callback >= 4, globus-gsi-proxy-core >= 6, globus-gss-assist >= 8, globus-gssapi-gsi >= 4"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign parallel-tests 1.11 tar-pax])
LT_INIT([dlopen win32-dll])

AC_PROG_CC_C99

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

AM_CONDITIONAL([CYGPATH_W_DEFINED], [test "$CYGPATH_W" != ""])
PKG_CHECK_MODULES([OPENSSL], [openssl >= 1.0],
    [OPENSSL_PKGCONFIG="openssl >= 1.0"])

AC_SUBST([OPENSSL_PKGCONFIG])
PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_PATH_PROGS([OPENSSL], [openssl])
AM_CONDITIONAL([ENABLE_TESTS], [test x"$OPENSSL" != x])

AC_PATH_PROGS([A2X], [a2x a2x.py])
AM_CONDITIONAL([BUILD_MANPAGES], [test x"$A2X" != x])

AC_CONFIG_FILES(
        globus-proxy-utils-uninstalled.pc
        globus-proxy-utils.pc
        Makefile
	programs/Makefile
        test/Makefile
        test/testcred.cnf
	version.h)
AC_OUTPUT
