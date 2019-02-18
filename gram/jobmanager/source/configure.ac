AC_PREREQ([2.60])

AC_INIT([globus_gram_job_manager],[15.4],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [2])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 15, globus-gsi-credential >= 5, globus-gass-cache >= 8, globus-gass-transfer >= 7, globus-gram-protocol >= 11, globus-gssapi-gsi >= 10, globus-gss-assist >= 8, globus-gsi-sysconfig >= 5, globus-callout >= 2, globus-xio >= 3, globus-xio-popen-driver >= 2, globus-rsl >= 9, globus-gram-job-manager-callout-error >= 2, globus-scheduler-event-generator >= 4"])

AC_SUBST([CLIENT_TEST_PACKAGE_DEPS], ["globus-common >= 14, globus-gram-protocol >= 11, globus-io >= 9, globus-rsl >= 9, globus-gss-assist >= 8, globus-gram-client >= 3, globus-gass-server-ez >= 2, globus-gass-transfer >= 7, globus-gssapi-gsi >= 10"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)


PKG_CHECK_MODULES([OPENSSL], [openssl >= 1.0],
    [OPENSSL_PKGCONFIG="openssl >= 1.0"])

AC_SUBST([OPENSSL_PKGCONFIG])
PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)
PKG_CHECK_MODULES([CLIENT_TEST_PACKAGE_DEP], $CLIENT_TEST_PACKAGE_DEPS)

AC_SUBST([JOBMANAGER_PATH], ["$PWD"])

PKG_CHECK_EXISTS([globus-gatekeeper], [
    GATEKEEPER_PATH="$($PKG_CONFIG --variable=path globus-gatekeeper)"])
AC_SUBST([GATEKEEPER_PATH])

PKG_CHECK_EXISTS([globus-gram-client-tools], [
    GRAM_CLIENT_TOOLS_PATH="$($PKG_CONFIG --variable=path globus-gram-client-tools)"])
AC_SUBST([GRAM_CLIENT_TOOLS_PATH])

PKG_CHECK_EXISTS([globus-gass-copy], [
    GASS_COPY_PATH="$($PKG_CONFIG --variable=path globus-gass-copy)"])
AC_SUBST([GASS_COPY_PATH])

PKG_CHECK_EXISTS([globus-gass-cache-program], [
    GASS_CACHE_PATH="$($PKG_CONFIG --variable=path globus-gass-cache-program)"])
AC_SUBST([GASS_CACHE_PATH])

PKG_CHECK_EXISTS([globus-gram-job-manager-scripts], [
    GRAM_JOB_MANAGER_SCRIPT_PATH="$($PKG_CONFIG --variable=path globus-gram-job-manager-scripts)"
    GRAM_JOB_MANAGER_SCRIPT_PERL5LIB="$($PKG_CONFIG --variable=perl5lib globus-gram-job-manager-scripts)"])
AC_SUBST([GRAM_JOB_MANAGER_SCRIPT_PATH])
AC_SUBST([GRAM_JOB_MANAGER_SCRIPT_PERL5LIB])

PKG_CHECK_EXISTS([globus-gram-job-manager-fork], [
    GRAM_JOB_MANAGER_FORK_PERL5LIB="$($PKG_CONFIG --variable=perl5lib globus-gram-job-manager-fork)"])
AC_SUBST([GRAM_JOB_MANAGER_FORK_PERL5LIB])

PKG_CHECK_EXISTS([globus-gram-protocol], [
    GRAM_PROTOCOL_PERL5LIB="$($PKG_CONFIG --variable=perl5lib globus-gram-protocol)"])
AC_SUBST([GRAM_PROTOCOL_PERL5LIB])

PKG_CHECK_EXISTS([globus-common], [
    COMMON_PERL5LIB="$($PKG_CONFIG --variable=perl5lib globus-common)"])
AC_SUBST([COMMON_PERL5LIB])

PKG_CHECK_EXISTS([globus-proxy-utils], [
    GSI_PROXY_UTILS_PATH="$($PKG_CONFIG --variable=path globus-proxy-utils)"])
AC_SUBST([GSI_PROXY_UTILS_PATH])

PKG_CHECK_EXISTS([globus-gsi-cert-utils], [
    GSI_CERT_UTILS_PATH="$($PKG_CONFIG --variable=path globus-gsi-cert-utils)"])
AC_SUBST([GSI_CERT_UTILS_PATH])

PKG_CHECK_EXISTS([globus-xio-gsi-driver], [
    GLOBUS_XIO_GSI_DRIVER_DLOPEN="$($PKG_CONFIG --variable=dlopen globus-xio-gsi-driver)"])
AC_SUBST([GLOBUS_XIO_GSI_DRIVER_DLOPEN])

PKG_CHECK_EXISTS([globus-xio-popen-driver], [
    GLOBUS_XIO_POPEN_DRIVER_DLOPEN="$($PKG_CONFIG --variable=dlopen globus-xio-popen-driver)"])
AC_SUBST([GLOBUS_XIO_POPEN_DRIVER_DLOPEN])

if test -z "$GLOBUS_XIO_GSI_DRIVER_DLOPEN"; then
    PREOPEN_FORCE="-dlpreopen force"
fi
AC_SUBST([PREOPEN_FORCE])
AC_CANONICAL_BUILD
AC_CANONICAL_HOST

case $build:$host in 
    *cygwin*:*mingw*)
        use_cygpath_w=true
        ;;
    *)
        use_cygpath_w=false
        ;;
esac

AM_CONDITIONAL([USE_CYGPATH_W], [test "$use_cygpath_w" = "true"])
    

AC_PATH_PROGS([OPENSSL], [openssl])
AM_CONDITIONAL([ENABLE_TESTS], [test "x$OPENSSL" != "x"])

AC_PATH_PROGS([DOXYGEN], doxygen)
AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])

if test "X$GLOBUS_VERSION" = "X"; then
    if test -x ${GLOBUS_LOCATION-/usr}/bin/globus-version; then
        GLOBUS_VERSION="`${GLOBUS_LOCATION-/usr}/bin/globus-version`"
    else
        GLOBUS_VERSION="unknown"
    fi
fi
AC_SUBST(GLOBUS_VERSION)

AM_PATH_XML2
if test "$ac_cv_have_libxml2" = "yes"; then
    AC_DEFINE(HAVE_LIBXML2,  [1], [Define to 1 if you have libxml2])
fi

dnl Some preprocessor tricks to get CMSG_SPACE and co. defined in <sys/socket.h>
case $host in
       *solaris*)
           AC_DEFINE(__EXTENSIONS__)
           AC_DEFINE(_XOPEN_SOURCE)
           AC_DEFINE(_XOPEN_SOURCE_EXTENDED)
       ;;
esac

eval "LOGDIR=$localstatedir/log/globus"
AC_SUBST(LOGDIR)

eval AUDITDIR="$localstatedir/lib/globus/gram-audit"
AC_SUBST(AUDITDIR)

AC_PROG_LEX
AC_PROG_YACC

AC_PATH_PROGS([A2X], [a2x a2x.py])
AM_CONDITIONAL(BUILD_MANPAGES, [test x"$A2X" != x])

AC_CONFIG_FILES([
	globus-gram-job-manager-uninstalled.pc
	globus-gram-job-manager.pc
	scripts/Makefile
	rvf/globus-rvf-edit
        rvf/Makefile
        seg/Makefile
        test/Makefile
        test/testcred.cnf
        test/client/Makefile
        test/jobmanager/failure_test/Makefile
        test/jobmanager/Makefile
        test/jobmanager/rsl_size_test/Makefile
        test/jobmanager/stdio_test/Makefile
        test/jobmanager/submit_test/Makefile
        test/jobmanager/user_test/Makefile
        globus-gram-job-manager.conf
        gram.logrotate
        Makefile
        version.h
])
AC_CONFIG_FILES([globus-personal-gatekeeper],
    [chmod a+x globus-personal-gatekeeper])
AC_CONFIG_FILES(
        [test/jobmanager/job-manager-script-test.pl], [chmod a+x test/jobmanager/job-manager-script-test.pl])
AC_CONFIG_FILES(
        [test/jobmanager/submit_test/globus-gram-job-manager-submit-test.pl], [chmod a+x test/jobmanager/submit_test/globus-gram-job-manager-submit-test.pl])
AC_CONFIG_FILES(
        [test/jobmanager/failure_test/globus-gram-job-manager-failure-test.pl], [chmod a+x test/jobmanager/failure_test/globus-gram-job-manager-failure-test.pl])
AC_CONFIG_FILES(
        [test/jobmanager/rsl_size_test/globus-gram-job-manager-rsl-size-test.pl], [chmod a+x test/jobmanager/rsl_size_test/globus-gram-job-manager-rsl-size-test.pl])
AC_CONFIG_FILES(
        [test/gram-test-wrapper],
        [chmod a+x test/gram-test-wrapper])
AC_OUTPUT
