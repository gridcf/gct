AC_PREREQ([2.60])

AC_INIT([globus_gram_job_manager_fork],[3.1],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [2])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-xio >= 3, globus-scheduler-event-generator >= 4, globus-gram-protocol >= 11"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])
AM_PROG_CC_C_O
AC_PROG_MKDIR_P

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_PATH_PROG(MPIEXEC, mpiexec, no)
if test "$MPIEXEC" = "no" ; then
    AC_MSG_WARN([Cannot locate mpiexec])
fi

AC_PATH_PROG(MPIRUN, mpirun, no)
if test "$MPIRUN" = "no" ; then
    AC_MSG_WARN([Cannot locate mpirun])
fi

# SoftEnv installation directory
AC_ARG_WITH(softenv-dir,
   AC_HELP_STRING([--with-softenv-dir], [softenv_install_dir]),
   [SOFTENV_DIR=$withval],
   [SOFTENV_DIR=''])

AC_SUBST(SOFTENV_DIR)

AC_ARG_WITH(globus-state-dir,
	AC_HELP_STRING(
		[--with-globus-state-dir=PATH],
		[Path to Globus state files]),
	globusstatedir="$withval",
	globusstatedir=$localstatedir)

# GRAM-333: SEG config in installer has variables that aren't resolved at
# runtime
globusstatedir="`eval echo $globusstatedir`"
AC_SUBST(globusstatedir)

AC_ARG_WITH(seg,
	AC_HELP_STRING(
		[--with-seg=yes|no],
		[Enable GRAM configuration with the SEG module]),
	SEG_ENABLED="$withval",
	SEG_ENABLED="no")
case "$SEG_ENABLED" in
	yes)
	    SEG_CLAUSE="-seg-module fork"
	    ;;
	no)
	    SEG_CLAUSE=""
	    ;;
	*)
	    AC_MSG_ERROR([Invalid argument to --with-seg])
	    ;;
esac

AC_SUBST(SEG_CLAUSE)
AM_CONDITIONAL(SEG_ENABLED_COND, [test "$SEG_ENABLED" = "yes"])

AC_PATH_PROGS([A2X], [a2x a2x.py])
AM_CONDITIONAL(BUILD_MANPAGES, [test "x$A2X" != x])

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
        globus-gram-job-manager-fork-uninstalled.pc
        globus-gram-job-manager-fork.pc
	Makefile
	globus-fork.conf
	starter/Makefile
	seg/Makefile
	version.h)
AC_OUTPUT
