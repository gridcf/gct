AC_PREREQ([2.60])

AC_INIT([globus_gram_job_manager_lsf],[3.0],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [2])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-scheduler-event-generator >= 4"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

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

AC_ARG_WITH([lsf-profile],
    AC_HELP_STRING(
	[--with-lsf-profile=FILE],
        [Use LSF profile located at PATH ${LSF_ENVDIR:-/etc}/profile.lsf]),
        [LSF_PROFILE=$withval],
	[LSF_PROFILE="${LSF_ENVDIR:-/etc}/profile.lsf"
         AC_MSG_WARN([Using default lsf profile of $LSF_PROFILE])])

AC_SUBST(LSF_PROFILE)

if test ! -r "${LSF_PROFILE}"; then
    AC_MSG_WARN([Unable to read lsf.profile, manual configuration needed])
else
    . "${LSF_PROFILE}"
fi

AC_PATH_PROG(BSUB, bsub, no)
if test "$BSUB" = "no" ; then
    AC_MSG_WARN([Cannot locate bsub])
fi

AC_PATH_PROG(BQUEUES, bqueues, no)
if test "$BQUEUES" = "no"; then
    AC_MSG_WARN([Cannot locate bqueues])
fi

AC_PATH_PROG(BJOBS, bjobs, no)
if test "$BJOBS" = "no"; then
    AC_MSG_WARN([Cannot locate bjobs])
fi

AC_PATH_PROG(BKILL, bkill, no)
if test "$BKILL" = "no"; then
    AC_MSG_WARN([Cannot locate bkill])
fi

AC_PATH_PROG(BHIST, bhist, no)
if test "$BHIST" = "no"; then
    AC_MSG_WARN([Cannot locate bhist])
fi

AC_PATH_PROG(BACCT, bacct, no)
if test "$BACCT" = "no"; then
    AC_MSG_WARN([Cannot locate bacct])
fi

log_guess="${LSF_ENVDIR%/}"
log_guess="${log_guess%/*}"
log_guess="$(echo "${log_guess}/work/"*)/logdir"
if test ! -d "$log_guess"; then
    log_guess="no"
fi
AC_ARG_WITH(log-path,
    AC_HELP_STRING(
        [--with-log-path=DIRECTORY],
        [Parse LSF logs in DIRECTORY when using the SEG [[$log_guess]]]),
    [LSF_LOG_PATH=$withval],
    [LSF_LOG_PATH=${log_guess}])
AC_SUBST(LSF_LOG_PATH)

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
	yes|no)
            :
            ;;
	*)
	    AC_MSG_ERROR([Invalid argument to --with-seg])
	    ;;
esac

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


AM_CONDITIONAL(SEG_ENABLED_COND, [test "$SEG_ENABLED" = "yes"])

AC_CONFIG_FILES(
        globus-gram-job-manager-lsf-uninstalled.pc
        globus-gram-job-manager-lsf.pc
        globus-lsf.conf
        Makefile
        seg/Makefile
	version.h)
AC_OUTPUT
