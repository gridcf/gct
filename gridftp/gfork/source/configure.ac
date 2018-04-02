AC_PREREQ([2.60])

AC_INIT([globus_gfork], [5.0], [https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [5])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-xio >= 3"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_CANONICAL_HOST

case $host in
    *mingw* | win* | *cygwin*)
        GFORK_EXE=""
        ;;
    *)
        GFORK_EXE=gfork
        ;;
esac
AC_SUBST(GFORK_EXE)

AC_OUTPUT(
	Makefile
        version.h
        globus-gfork.pc
        globus-gfork-uninstalled.pc
)
