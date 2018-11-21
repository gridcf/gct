AC_PREREQ([2.60])

AC_INIT([globus_ftp_control], [9.4], [https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [8])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-gss-assist >= 11, globus-gssapi-gsi >= 13, globus-io >= 11, globus-xio >= 3, globus-gssapi-error >= 4"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

AM_PROG_CC_C_O
AC_PROG_CC_C99

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

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
PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)
PKG_CHECK_EXISTS([globus-xio-gsi-driver >= 4], [
    GSI_DRIVER_CFLAGS="$($PKG_CONFIG --cflags globus-xio-gsi-driver)"
    GSI_DRIVER_DLOPEN="$($PKG_CONFIG --variable=dlopen globus-xio-gsi-driver)"
    GSI_DRIVER_DLPREOPEN="$($PKG_CONFIG --variable=dlpreopen globus-xio-gsi-driver)"])
AC_SUBST([GSI_DRIVER_CFLAGS])
AC_SUBST([GSI_DRIVER_DLOPEN])
AC_SUBST([GSI_DRIVER_DLPREOPEN])

AC_CHECK_HEADERS([netinet/tcp.h])

AC_PATH_PROGS([OPENSSL], openssl)
AM_CONDITIONAL([ENABLE_TESTS], [test "x$OPENSSL" != x])
AC_PATH_PROGS([DOXYGEN], doxygen)

AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])
AC_CONFIG_FILES(
        globus-ftp-control.pc
        globus-ftp-control-uninstalled.pc
        Makefile
        Doxyfile
	version.h
        test/Makefile
        test/testcred.cnf)

AC_OUTPUT
