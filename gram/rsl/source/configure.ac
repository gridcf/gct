AC_PREREQ([2.60])

AC_INIT([globus_rsl],[11.1],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [9])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_PATH_PROGS([DOXYGEN], doxygen)

AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])

AM_MAINTAINER_MODE
AC_PROG_LEX
if test "x$LEX" != xflex; then
    LEX="$SHELL \$(abs_top_srcdir)/build-aux/missing flex"
    AC_SUBST([LEX_OUTPUT_ROOT], [lex.yy])
    AC_SUBST([LEXLIB], [''])
fi
AC_PROG_YACC

AC_CONFIG_FILES(
        globus-rsl-uninstalled.pc
        globus-rsl.pc
        Makefile
        test/Makefile
        Doxyfile
	version.h)
AC_OUTPUT
