AC_PREREQ([2.60])

AC_INIT([globus_gram_job_manager_condor],[3.0],[https://github.com/gridcf/gct/issues])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [2])
AC_SUBST([PACKAGE_DEPS], [""])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

AC_PATH_PROGS(CONDOR_SUBMIT, condor_submit, no)
if test "$CONDOR_SUBMIT" = "no" ; then
    AC_MSG_WARN([Cannot locate condor_submit])
fi

AC_PATH_PROGS(CONDOR_RM, condor_rm, no)
if test "$CONDOR_RM" = "no"; then
    AC_MSG_WARN([Cannot locate condor_rm])
fi

AC_ARG_WITH(condor-os,
    AC_HELP_STRING(
            [--with-condor-os=OPSYS],
            [Submit GRAM jobs to condor with an OpSys == OPSYS requirement]),
	    [if test "$withval" != yes; then
	         CONDOR_OS=$withval
	     else
	         CONDOR_OS=no
	     fi],
	    [CONDOR_OS=no])

AC_ARG_WITH(condor-arch,
    AC_HELP_STRING(
            [--with-condor-arch=ARCH],
            [Submit GRAM jobs to condor with an Arch == Arch requirement]),
	    [if test "$withval" != yes; then
	         CONDOR_ARCH=$withval
	     else
	         CONDOR_ARCH=no
	     fi],
	    [CONDOR_ARCH=no])

AC_ARG_WITH(check-vanilla-files,
    AC_HELP_STRING(
            [ --with-check-vanilla-files],
            [Perform file permission tests for stdin, stdout, stderr, and
            executable files prior to submitting to condor]),
	    [CONDOR_CHECK_VANILLA_FILES=$withval],
	    [CONDOR_CHECK_VANILLA_FILES=no])

AC_ARG_WITH(mpi-script,
    AC_HELP_STRING(
            [--with-mpi-script=MPI-SCRIPT],
            [Support parallel MPI jobs via the MPI-SCRIPT command]),
	    [if test "$withval" != yes; then
	         CONDOR_MPI_SCRIPT=$withval
	     else
	         CONDOR_MPI_SCRIPT=no
	     fi],
	    [CONDOR_MPI_SCRIPT=no])


UNAME_MACHINE=`(uname -m) 2> /dev/null ` || UNAME_MACHINE=unknown
UNAME_RELEASE=`(uname -r) 2> /dev/null ` || UNAME_RELEASE=unknown
UNAME_SYSTEM=`(uname -s) 2> /dev/null ` || UNAME_SYSTEM=unknown
UNAME_VERSION=`(uname -v) 2> /dev/null ` || UNAME_VERSION=unknown

if test $CONDOR_OS = 'no'; then
    case "${UNAME_MACHINE}:${UNAME_SYSTEM}:${UNAME_RELEASE}" in
       *:FreeBSD:6*)
         CONDOR_OS=FREEBSD6
         ;;
       *:Linux:*)
	 CONDOR_OS=LINUX
	 ;;
       *:OSF1:*)
	 CONDOR_OS=OSF1
	 ;;
       *:SunOS:5.[[6-9]])
	 CONDOR_OS=SOLARIS`echo ${UNAME_RELEASE} | sed -e 's:5\.\(.\):2\1:'`
	 ;;
       *:SunOS:5.10)
         CONDOR_OS=SOLARIS5.10
         ;;
       *:HP-UX:*)
         CONDOR_OS=HPUX11
         ;;
       *:Darwin:*)
         CONDOR_OS=OSX
         ;;
       *:AIX:*)
         CONDOR_OS=AIX${UNAME_VERSION}${UNAME_RELEASE}
         ;;
       *)
         AC_MSG_WARN([unknown CONDOR_OS])
	 ;;
    esac
fi

if test $CONDOR_ARCH = 'no'; then
    case "${UNAME_MACHINE}:${UNAME_SYSTEM}:${UNAME_RELEASE}" in
        alpha:*:*)
	  CONDOR_ARCH=ALPHA
	  ;;
        x86_64:*:*)
          CONDOR_ARCH=x86_64
          ;;
        i?86*:*:*)
	  CONDOR_ARCH=INTEL
	  ;;
        sun4*:*:*)
	  CONDOR_ARCH=`uname -m | sed -e 's:sun4\(.\):SUN4\1:'`
	  ;;
        9000/?00:*:*)
          CONDOR_ARCH=HPPA2
          ;;
        ia64:*:*)
          CONDOR_ARCH=IA64
          ;;
        "Power Macintosh":*:*)
          CONDOR_ARCH=PPC
          ;;
         *:AIX:[[23]]) 
          CONDOR_ARCH=PWR3II 
          ;; 
       *)
         AC_MSG_WARN([unknown CONDOR_ARCH])
      esac
fi

if test "$CONDOR_CONFIG" = ''; then
    AC_MSG_WARN([CONDOR_CONFIG not set in environment, condor will search default paths])
fi

AM_CONDITIONAL(CONDOR_CONFIG_COND, [test "$CONDOR_CONFIG" != ""])
AM_CONDITIONAL(CONDOR_MPI_SCRIPT_COND, [test "$CONDOR_MPI_SCRIPT" != "no"])
AM_CONDITIONAL(CHECK_VANILLA_FILES_COND, [test "$CONDOR_CHECK_VANILLA_FILES" = "yes"])
AC_SUBST(CONDOR_CONFIG)
AC_SUBST(CONDOR_OS)
AC_SUBST(CONDOR_ARCH)
AC_SUBST(CONDOR_CHECK_VANILLA_FILES)
AC_SUBST(CONDOR_MPI_SCRIPT)

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
        globus-gram-job-manager-condor-uninstalled.pc
        globus-gram-job-manager-condor.pc
        globus-condor.conf
        Makefile)

AC_OUTPUT
