AC_PREREQ([2.60])
AC_INIT([globus_xioperf], [5.1],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [2])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-xio >= 3"])

m4_include(dirt.sh)
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([PACKAGE_DEP],$PACKAGE_DEPS)

AC_OUTPUT(
        globus-xioperf-uninstalled.pc
        globus-xioperf.pc
	Makefile
        version.h
)
