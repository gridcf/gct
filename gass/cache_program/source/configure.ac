AC_PREREQ([2.60])

AC_INIT([globus_gass_cache_program],[7.0],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [2])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-gass-cache >= 8, globus-gass-copy >= 8, globus-gass-transfer >= 7, globus-gass-server-ez >= 4, globus-gram-client >= 12"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign parallel-tests 1.11 tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_ARG_WITH([perlmoduledir],
AC_HELP_STRING([--with-perlmoduledir=DIR],
[perl module directory [[PREFIX/lib/perl]]]),
[
    if test x$withval = "xno" -o x$withval = "xyes" ; then
        AC_MSG_ERROR([--with-perlmoduledir requires an argument])
    fi
    perlmoduledir=$withval
],
[
    perlmoduledir='${libdir}/perl'
])
AC_SUBST(perlmoduledir)

AC_CONFIG_FILES(
        globus-gass-cache-program-uninstalled.pc
        globus-gass-cache-program.pc
        Makefile
	version.h)
AC_CONFIG_FILES([globus-gass-cache], [chmod a+x globus-gass-cache])
AC_CONFIG_FILES([globus-gass-cache-destroy],  [chmod a+x globus-gass-cache-destroy])
AC_CONFIG_FILES([globus-gass-cache-util.pl], [chmod a+x globus-gass-cache-util.pl])
AC_OUTPUT
