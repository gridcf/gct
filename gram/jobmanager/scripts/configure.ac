AC_PREREQ([2.60])

AC_INIT([globus_gram_job_manager_scripts],[7.2],[https://github.com/gridcf/gct/issues])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [3])
AC_SUBST([PACKAGE_DEPS], [""])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

AC_PROG_MKDIR_P

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

AC_PATH_PROGS([A2X], [a2x a2x.py])
AM_CONDITIONAL(BUILD_MANPAGES, [test "x$A2X" != x])

AC_CONFIG_FILES(
        globus-gram-job-manager-scripts-uninstalled.pc
        globus-gram-job-manager-scripts.pc
        Makefile)
AC_CONFIG_FILES([globus-gatekeeper-admin],
    [chmod a+x globus-gatekeeper-admin])
AC_CONFIG_FILES([globus-job-manager-script.pl],
    [chmod a+x globus-job-manager-script.pl])
AC_OUTPUT
