AC_PREREQ([2.60])

AC_INIT([globus_xio_gsi_driver],[5.2],[https://github.com/gridcf/gct/issues/])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [5])
AC_SUBST([PACKAGE_DEPS], ["globus-xio >= 3, globus-gss-assist >= 11, globus-gssapi-error >= 4, globus-gssapi-gsi >= 13, globus-common >= 14"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

AM_PROG_CC_C_O
AC_PROG_CC_C99

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_PATH_PROGS([DOXYGEN], doxygen)

AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])

AC_CONFIG_FILES(
        globus-xio-gsi-driver.pc
        globus-xio-gsi-driver-uninstalled.pc
	Makefile
        Doxyfile
        version.h
)
AC_OUTPUT
