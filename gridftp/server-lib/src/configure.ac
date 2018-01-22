AC_PREREQ([2.60])

AC_INIT([globus_gridftp_server_control],[7.0],[https://github.com/globus/globus-toolkit/issues])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [7])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-xio >= 3, globus-xio-gsi-driver >= 2, globus-xio-pipe-driver >= 2, globus-gss-assist >= 8, globus-gssapi-gsi >= 10, globus-gsi-openssl-error >= 2, globus-gssapi-error >= 4"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_CONFIG_FILES(
        globus-gridftp-server-control.pc
        globus-gridftp-server-control-uninstalled.pc
        Makefile
	version.h
        test/Makefile)
AC_OUTPUT
