AC_PREREQ([2.60])

AC_INIT([globus_gass_server_ez],[6.1],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [4])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-gass-transfer >= 7, globus-gssapi-gsi >= 10, globus-gss-assist >= 8"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_PATH_PROGS([DOXYGEN], doxygen)

AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])
AC_CONFIG_FILES(
        [globus-gass-server-ez-uninstalled.pc
        globus-gass-server-ez.pc
        Makefile
        Doxyfile
	version.h])
AC_CONFIG_FILES(
        [globus-gass-server-shutdown],
        [chmod a+x globus-gass-server-shutdown])
AC_OUTPUT
