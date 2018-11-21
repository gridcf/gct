AC_PREREQ([2.60])

AC_INIT([globus_gss_assist], [12.2],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [9])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-gsi-sysconfig >= 7, globus-gsi-cert-utils >= 8, globus-gssapi-gsi >= 13, globus-callout >= 2, globus-gsi-credential >= 6"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

AM_PROG_CC_C_O
AC_PROG_CC_C99

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_SUBST(PKG_DEP_CFLAGS)
AC_SUBST(PKG_DEP_LIBS)

AM_CONDITIONAL([CYGPATH_W_DEFINED], [
    case $host in
        *mingw32*)
        test x"$CYGPATH_W" != x;
        ;;
        *)
        false
        ;;
    esac])

AC_PATH_PROGS([DOXYGEN], doxygen)

AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])

AC_PATH_PROGS([OPENSSL], openssl)
AM_CONDITIONAL([ENABLE_TESTS], [test "$OPENSSL" != ""])

AC_PATH_PROGS(PERL, [perl perl5])

AC_PATH_PROGS([A2X], [a2x a2x.py])
AM_CONDITIONAL(BUILD_MANPAGES, [test "x$A2X" != x])

AC_CONFIG_FILES([
        globus-gss-assist-uninstalled.pc
        globus-gss-assist.pc
        Makefile
        Doxyfile
        programs/Makefile
        test/Makefile
        test/testcred.cnf
        version.h])

AC_CONFIG_FILES(
        [programs/grid-mapfile-add-entry],
        [chmod a+x programs/grid-mapfile-add-entry])
AC_CONFIG_FILES(
        [programs/grid-mapfile-delete-entry],
        [chmod a+x programs/grid-mapfile-delete-entry])
AC_CONFIG_FILES(
        [programs/grid-mapfile-check-consistency],
        [chmod a+x programs/grid-mapfile-check-consistency])

AC_OUTPUT
