AC_PREREQ([2.60])

AC_INIT([globus_gram_job_manager_pbs],[3.1],[https://github.com/gridcf/gct/issues])
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

AC_PROG_CC_C99

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_PATH_PROG(MPIEXEC, mpiexec, no)
if test "$MPIEXEC" = "no" ; then
    AC_MSG_WARN([Cannot locate mpiexec])
fi

AC_PATH_PROG(MPIRUN, mpirun, no)
if test "$MPIRUN" = "no" ; then
    AC_MSG_WARN([Cannot locate mpirun])
fi

AC_PATH_PROG(QDEL, qdel, no)
if test "$QDEL" = "no" ; then
    AC_MSG_WARN([Cannot locate qdel])
fi
AC_PATH_PROG(QSTAT, qstat, no)
if test "$QSTAT" = "no" ; then
    AC_MSG_WARN([Cannot locate qstat])
fi
AC_PATH_PROG(QSUB, qsub, no)
if test "$QSUB" = "no" ; then
    AC_MSG_WARN([Cannot locate qsub])
fi

AC_ARG_WITH(cluster,
    AC_HELP_STRING(
        [--without-cluster],
        [Disable using remote shell in job scripts]),
   [
     if test "$withval" != "no"; then
	 CLUSTER=1
     else
	 CLUSTER=0
     fi
   ],
   CLUSTER=1)

AC_SUBST(CLUSTER)

AC_ARG_WITH(cpu-per-node,
    AC_HELP_STRING(
        [--with-cpu-per-node=COUNT],
        [Run COUNT jobs per node when running a parallel job]),
   [ if test $withval -lt 0 2>/dev/null; then
       AC_MSG_ERROR(Invalid cpu-per-node count)
     fi
     CPU_PER_NODE=$withval
   ],
   CPU_PER_NODE=1)

AC_SUBST(CPU_PER_NODE)
       
AC_ARG_WITH(remote-shell,
    AC_HELP_STRING(
        [--with-remote-shell=REMOTE-SHELL-COMMAND|default],
        [Use REMOTE-SHELL-COMMAND to run job processes on PBS nodes in a cluster]),
            [ case "$withval" in
                default|yes)
                  AC_PATH_PROGS(REMOTE_SHELL, ssh rsh, no)
                  ;;
                *)
                  AC_PATH_PROG(REMOTE_SHELL, $withval, no)
                  ;;
              esac

              if test $REMOTE_SHELL = no; then
                  AC_MSG_ERROR("Cannot locate remote shell")
              fi
            ],
            [AC_PATH_PROGS(REMOTE_SHELL, ssh rsh, no)]
            )

AC_ARG_WITH(log-path,
    AC_HELP_STRING(
        [--with-log-path=DIRECTORY],
        [Parse PBS logs in DIRECTORY when using the SEG]),
    [PBS_LOG_PATH=$withval],
    [PBS_LOG_PATH=''])
AC_SUBST(PBS_LOG_PATH)

AM_CONDITIONAL(SEG_ENABLED_COND, [test "$SEG_ENABLED" = "yes"])

# SoftEnv installation directory
AC_ARG_WITH(softenv-dir,
   AC_HELP_STRING([--with-softenv-dir], [softenv_install_dir]),
   [SOFTENV_DIR=$withval],
   [SOFTENV_DIR=''])

AC_SUBST(SOFTENV_DIR)

xml_catalog_path="notset"
AC_ARG_WITH(
    xml-catalog,
    [  --with-xml-catalog=PATH       Path to XML catalog],
    [xml_catalog_path="$withval"],
    [xml_catalog_path="notset"])

AC_PATH_PROG(XSLTPROC, "xsltproc")
AC_PATH_PROG(XMLLINT, "xmllint")

if test "x$xml_catalog_path" != x"notset"; then
    if test ! -f "$xml_catalog_path"; then
        AC_MSG_ERROR([Invalid XML catalog path])
    else
        XML_CATALOG_PATH=$xml_catalog_path
        AC_SUBST(XML_CATALOG_PATH)
    fi
fi

AC_ARG_WITH(globus-state-dir,
	AC_HELP_STRING(
		[--with-globus-state-dir=PATH],
		[Path to Globus state files]),
	globusstatedir="$withval",
	globusstatedir=$localstatedir)
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


AM_CONDITIONAL(SEG_ENABLED_COND, [test "$SEG_ENABLED" = "yes"])

AC_PATH_PROG(XSLTPROC, "xsltproc")
AC_PATH_PROG(XMLLINT, "xmllint")
AC_MSG_CHECKING([docbook dtd])
DOCBOOK_DTD=http://www.oasis-open.org/docbook/xml/4.4/docbookx.dtd
docbook_dtd_available=no

if test "$XMLLINT" != no; then
    cat <<EOF | $XMLLINT --nonet --dtdvalid $DOCBOOK_DTD - > /dev/null 2>&1
<?xml version="1.0" encoding="ISO-8859-1"?>
<!DOCTYPE jobtitle PUBLIC "-//OASIS//DTD DocBook XML V4.4//EN" "http://www.oasis-open.org/docbook/xml/4.4/docbookx.dtd">
<jobtitle/>
EOF
    if test $? = 0; then
        docbook_dtd_available=yes
    fi
fi
AC_MSG_RESULT($docbook_dtd_available)
AC_SUBST(DOCBOOK_DTD)

AC_MSG_CHECKING([docbook xsl])
MAN_XSL=http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl
docbook_xsl_available=no

if test "$XSLTPROC" != no; then
    cat <<EOF | $XSLTPROC --nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl  - > /dev/null 2>&1
<?xml version="1.0" encoding="ISO-8859-1"?>
<!DOCTYPE jobtitle PUBLIC "-//OASIS//DTD DocBook XML V4.4//EN" "http://www.oasis-open.org/docbook/xml/4.4/docbookx.dtd">
<jobtitle/>
EOF
    if test $? = 0; then
        docbook_xsl_available=yes
    fi
fi
AC_MSG_RESULT($docbook_xsl_available)
AC_SUBST(MAN_XSL)

AM_CONDITIONAL(BUILD_MANPAGES, [
                test "$docbook_xsl_available" != "no" && \
                test "$docbook_dtd_available" != "no" && \
                test "$XSLTPROC" != "no" && \
                test "$XMLLINT" != "no"])

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

AC_CONFIG_LIBOBJ_DIR([seg])
AC_REPLACE_FUNCS([getline])

AC_CONFIG_FILES(
        globus-gram-job-manager-pbs-uninstalled.pc
        globus-gram-job-manager-pbs.pc
        Makefile
	globus-pbs.conf
	seg/Makefile
	version.h)
AC_OUTPUT
