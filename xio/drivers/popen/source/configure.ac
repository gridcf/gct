AC_PREREQ([2.60])

AC_INIT([globus_xio_popen_driver],[4.1],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [4])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-xio >= 3"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_CONFIG_FILES(
        globus-xio-popen-driver-uninstalled.pc
        globus-xio-popen-driver.pc
        Makefile
	version.h)
AC_OUTPUT
