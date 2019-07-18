AC_PREREQ([2.60])

AC_INIT([globus_gram_job_manager_sge],[3.1],[https://github.com/gridcf/gct/issues])
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

AC_ARG_WITH([default-pe],
    AC_HELP_STRING(
        [--with-default-pe=NAME],
        [Default Parallel Environment for execution of MPI jobs]),
    MPI_PE="$withval",
    [MPI_PE=''])

AC_SUBST(MPI_PE)

AC_ARG_WITH([available-pes],
    AC_HELP_STRING(
        [--with-available-pes="PE..."],
        [Use the list of parallel environments when generating error messages about missing or invalid parallel_environment RSL attributes]),
    [AVAILABLE_PES=$withval],
    [AVAILABLE_PES=''])
AC_SUBST(AVAILABLE_PES)

AC_ARG_WITH([pe-validation],
    AC_HELP_STRING(
        [--with-pe-validation],
        [Reject jobs which use PEs not defined in the available PE list]),
    [VALIDATE_PES=$withval],
    [VALIDATE_PES=no])
AC_SUBST(VALIDATE_PES)

AC_ARG_WITH([default-queue],
    AC_HELP_STRING(
        [--with-default-queue=NAME],
        [Use NAME as the default job queue]),
    DEFAULT_QUEUE="$withval",
    [DEFAULT_QUEUE=''])

AC_SUBST(DEFAULT_QUEUE)

AC_ARG_WITH([available-queues],
    AC_HELP_STRING(
        [--with-available-queues="QUEUE..."],
        [Use the list of queues when generating error messages about invalid queue RSL attributes]),
    [AVAILABLE_PES=$withval],
    [AVAILABLE_PES=''])
AC_SUBST(AVAILABLE_QUEUES)

AC_ARG_WITH([queue-validation],
    AC_HELP_STRING(
        [--with-queue-validation],
        [Reject jobs which use queues not defined in the available queue list]),
    [VALIDATE_QUEUES=$withval],
    [VALIDATE_QUEUES=no])
AC_SUBST(VALIDATE_QUEUES)

AC_ARG_ENABLE([sunmpi],
    AC_HELP_STRING([--disable-sunmpi], [Ignores Sun MPI]),
    [disable_sunmpi=$enableval],
    [disable_sunmpi=no])

AC_ARG_WITH([sge-root],
    AC_HELP_STRING([--with-sge-root=ROOT], [Use SGE installation in ROOT]),
    [SGE_ROOT="$withval"],
    [
        if test "$SGE_ROOT" = ""; then
            AC_MSG_WARN([SGE_ROOT is not set, use --with-sge-root])
            SGE_ROOT=undefined
        fi
    ])

AC_ARG_WITH([sge-cell],
    AC_HELP_STRING([--with-sge-cell=CELL], [Use SGE cell named CELL]),
    [SGE_CELL="$withval"],
    [
        if test "$SGE_CELL" = ""; then
            SGE_CELL=undefined
        fi
    ])

AC_ARG_WITH([sge-config],
    AC_HELP_STRING([--with-sge-config=FILE],
            [Look for SGE_ROOT and SGE_CELL definitions in FILE]),
    [SGE_CONFIG="$withval"])

if test -z "$SGE_CONFIG" -a ! -d "$SGE_ROOT/$SGE_CELL"; then
    AC_MSG_WARN([SGE_CELL $SGE_CELL is not defined in SGE_ROOT ($SGE_ROOT)])
fi

AC_ARG_WITH([reporting-file],
    AC_HELP_STRING(
        [--with-reporting-file=PATH],
        [When the SEG is enabled, look in the reporting file at PATH for
         SEG events]),
        [SGE_REPORTING_FILE="$withval"],
        [SGE_REPORTING_FILE=""])
AC_SUBST(SGE_REPORTING_FILE)
        
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


AM_CONDITIONAL(SEG_ENABLED_COND, [test "$SEG_ENABLED" = "yes"])

AC_SUBST(SGE_ROOT)
AC_SUBST(SGE_CELL)
AC_SUBST(SGE_CONFIG)

AC_PATH_PROGS(MPIRUN, mpirun, no)
AC_PATH_PROGS(SUN_MPRUN, mprun, no)
AC_PATH_PROGS(QSUB, qsub, no)
AC_PATH_PROGS(QSTAT, qstat, no)
AC_PATH_PROGS(QDEL, qdel, no)
AC_PATH_PROGS(QCONF, qconf, no)


for cmd in QSUB QSTAT QDEL QCONF; do
    eval "cmdval=\${$cmd}"
    if test "$cmdval" = "no"; then
        AC_MSG_WARN([Required SGE executable $cmd not found])
    fi
done

if test "$disable_sunmpi" = yes; then
    SUN_MPRUN=no
fi

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
        globus-gram-job-manager-sge-uninstalled.pc
        globus-gram-job-manager-sge.pc
	Makefile
        seg/Makefile
        globus-sge.conf
	version.h)
AC_OUTPUT
