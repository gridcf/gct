AC_PREREQ([2.60])

AC_INIT([globus_gssapi_gsi],[13.5],[https://github.com/globus/globus-toolkit/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [9])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-openssl-module >= 3, globus-gsi-openssl-error >= 2, globus-gsi-cert-utils >= 8, globus-gsi-credential >= 5, globus-gsi-callback >= 4, globus-gsi-proxy-core >= 6, globus-gsi-sysconfig >= 8"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

AC_PROG_CC_C99
AC_PROG_AWK
AC_CANONICAL_HOST
AM_CONDITIONAL([CYGPATH_W_DEFINED], [
    case $host in
        *mingw32*)
        test x"$CYGPATH_W" != x;
        ;;
        *)
        false
        ;;
    esac])

case "$host" in
*-*-linux* | *-*-gnu* | *-*-k*bsd*-gnu)
    use_symbol_labels="yes"
    ;;
*)
    use_symbol_labels="no"
    ;;
esac

PKG_CHECK_MODULES([OPENSSL], [openssl >= 1.0],
    [OPENSSL_PKGCONFIG="openssl >= 1.0"],
    [
        PKG_CHECK_MODULES([OPENSSL], [openssl101e])
        OPENSSL_PKGCONFIG="openssl101e"
    ])

AC_SUBST([OPENSSL_PKGCONFIG])
PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)
GLOBUS_THREAD_MODELS=$($PKG_CONFIG globus-common --variable=thread_models)
AC_SUBST(GLOBUS_THREAD_MODELS)

for model in $GLOBUS_THREAD_MODELS""; do
    if test x"$model" != x; then
        eval "have_${model}_module"=1
        thread_tests="${thread_tests:+$thread_tests }gssapi-thread-test-$model"
    fi
done

PKG_CHECK_EXISTS([globus-thread-pthread], [
pthread_dlpreopen=$($PKG_CONFIG --variable=dlpreopen globus-thread-pthread)
if test "$have_pthread_module"x = x; then
    have_pthread_module=1
    thread_tests="${thread_tests:+$thread_tests }gssapi-thread-test-pthread"
fi])

PKG_CHECK_EXISTS([globus-thread-windows], [
windows_dlpreopen=$($PKG_CONFIG --variable=dlpreopen globus-thread-windows)
if test "$have_windows_module"x = x; then
    have_windows_module=1
    thread_tests="${thread_tests:+$thread_tests }gssapi-thread-test-windows"
fi])


AC_SUBST([THREAD_TEST_DLPREOPEN], [$pthread_dlpreopen $windows_dlpreopen])
AC_SUBST([THREAD_TESTS], [$thread_tests])

AC_PATH_PROGS([OPENSSL], openssl)
AC_PATH_PROGS([DOXYGEN], doxygen)

AM_CONDITIONAL([ENABLE_TESTS], [test "$OPENSSL" != ""])
AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])

SAVE_LIBS="$LIBS"
LIBS="$LIBS $PACKAGE_DEP_LIBS"
AC_MSG_CHECKING([if we can link with internal OpenSSL APIs])
AC_LINK_IFELSE(
    [AC_LANG_PROGRAM(
        [#include <openssl/ssl.h>
        extern int ssl3_setup_buffers(SSL *s);],
        [ssl3_setup_buffers(NULL);])],
    [ac_cv_can_link_with_openssl_internal_api=yes],
    [ac_cv_can_link_with_openssl_internal_api=no])
AC_MSG_RESULT([$ac_cv_can_link_with_openssl_internal_api])
LIBS="$SAVE_LIBS"

if test "$ac_cv_can_link_with_openssl_internal_api" = yes; then
    AC_DEFINE([LINK_WITH_INTERNAL_OPENSSL_API],[1],[Define to `1' to use ss3_setup_buffers to import and export gss_cred_id_t structures])
fi

AM_CONDITIONAL(
    [EXPORT_SEC_CONTEXT_WORKS],
    [test "$ac_cv_can_link_with_openssl_internal_api" = yes])

AM_CONDITIONAL(USE_SYMBOL_LABELS, test x$use_symbol_labels = "xyes")

AC_CONFIG_FILES(
        globus-gssapi-gsi.pc
        globus-gssapi-gsi-uninstalled.pc
	Makefile
        version.h
        library/Doxyfile
        library/Makefile
        test/Makefile
        test/testcred.cnf
)
AC_OUTPUT
