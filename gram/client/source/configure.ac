AC_PREREQ([2.60])

AC_INIT([globus_gram_client],[14.3],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [11])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-gram-protocol >= 11, globus-io >= 9, globus-rsl >= 9, globus-gss-assist >= 8"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_SUBST([GLOBUS_XIO_GSI_DLPREOPEN], ["$("$PKG_CONFIG" globus-xio-gsi-driver --variable=dlpreopen)"])

GLOBUS_GRAM_JOB_MANAGER_PATH=$($PKG_CONFIG --variable=path globus-gram-job-manager)
AC_SUBST([GLOBUS_GRAM_JOB_MANAGER_PATH])

PROXY_UTILS_PATH=$($PKG_CONFIG --variable=path globus-proxy-utils)
AC_SUBST([PROXY_UTILS_PATH])

CERT_UTILS_PATH=$($PKG_CONFIG --variable=path globus-gsi-cert-utils)
AC_SUBST([CERT_UTILS_PATH])

AC_CANONICAL_HOST
AC_CANONICAL_BUILD
case $build:$host in
    *cygwin*:*mingw*)
        use_cygpath_w=yes
        ;;
    *)
        use_cygpath_w=no
        ;;
esac
AM_CONDITIONAL([USE_CYGPATH_W], [test yes = "$use_cygpath_w"])
    
AC_PATH_PROGS([OPENSSL], openssl)
AM_CONDITIONAL([ENABLE_TESTS], [test "x$OPENSSL" != x])
AC_PATH_PROGS([DOXYGEN], doxygen)

AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])


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
        globus-gram-client-uninstalled.pc
        globus-gram-client.pc
        Makefile
        test/Makefile
        test/testcred.cnf
        Doxyfile
	version.h)

AC_OUTPUT
