AC_PREREQ([2.60])

AC_INIT([globus_gass_copy],[10.5],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [8])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 15, globus-ftp-client >= 7, globus-ftp-control >= 4, globus-gsi-sysconfig >= 4, globus-gass-transfer >= 7, globus-io >= 8, globus-gssapi-gsi >= 9, globus-gssapi-error >= 4"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

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

AM_CONDITIONAL([MINGW32], [
    case $host in 
        *mingw32*)
        true
        ;;
    *)
        false
        ;;
esac])

PKG_CHECK_MODULES([OPENSSL], [openssl >= 1.0],
    [OPENSSL_PKGCONFIG="openssl >= 1.0"])

AC_SUBST([OPENSSL_PKGCONFIG])

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

PKG_CHECK_EXISTS([globus-xio-gsi-driver >= 2], [
    GSI_DRIVER_CFLAGS="$($PKG_CONFIG --cflags globus-xio-gsi-driver)"
    GSI_DRIVER_DLOPEN="$($PKG_CONFIG --variable=dlopen globus-xio-gsi-driver)"
    GSI_DRIVER_DLPREOPEN="$($PKG_CONFIG --variable=dlpreopen globus-xio-gsi-driver)"])
PKG_CHECK_EXISTS([globus-xio-popen-driver >= 3], [
    POPEN_DRIVER_DLOPEN="$($PKG_CONFIG --variable=dlopen globus-xio-popen-driver)"])
PKG_CHECK_EXISTS([globus-xio-pipe-driver >= 2], [
    PIPE_DRIVER_LIBS="$($PKG_CONFIG --variable=dlopen globus-xio-pipe-driver)"])
AC_SUBST([GSI_DRIVER_CFLAGS])
AC_SUBST([GSI_DRIVER_DLOPEN])
AC_SUBST([GSI_DRIVER_DLPREOPEN])
AC_SUBST([POPEN_DRIVER_DLOPEN])
AC_SUBST([PIPE_DRIVER_LIBS])

PKG_CHECK_EXISTS([globus-gridftp-server], [
    GRIDFTP_SERVER_PATH="$($PKG_CONFIG --variable=path globus-gridftp-server)"
    GRIDFTP_SERVER_EXE="$($PKG_CONFIG --variable=executable globus-gridftp-server)"])
if test "x$GRIDFTP_SERVER_EXE" = x; then
    GRIDFTP_SERVER_EXE=globus-gridftp-server
fi
AC_SUBST([GRIDFTP_SERVER_PATH])
AC_SUBST([GRIDFTP_SERVER_EXE])

AC_PATH_PROGS([OPENSSL], openssl)
AC_PATH_PROGS([DOXYGEN], doxygen)

AM_CONDITIONAL([ENABLE_TESTS], [test x"$OPENSSL" != "x"])
AM_CONDITIONAL([ENABLE_DOXYGEN], [test x"$DOXYGEN" != "x"])

AC_MSG_CHECKING([Toolkit version])
if test "X$GLOBUS_TOOLKIT_ID" = "X"; then
    if test "X$GLOBUS_VERSION" = "X"; then
        if test -x ${GLOBUS_LOCATION-/usr}/bin/globus-version; then
            GLOBUS_TOOLKIT_ID="`${GLOBUS_LOCATION-/usr}/bin/globus-version -full`"
        else
            GLOBUS_TOOLKIT_ID="unknown"
        fi
    else
        GLOBUS_TOOLKIT_ID="Grid Community Toolkit $GLOBUS_VERSION"
    fi
fi
AC_MSG_RESULT($GLOBUS_TOOLKIT_ID)
AC_SUBST(GLOBUS_TOOLKIT_ID)

AC_PATH_PROGS(A2X, [a2x a2x.py])
AM_CONDITIONAL([BUILD_MANPAGES], [test "x$A2X" != "x"])

AC_CONFIG_FILES(
        Doxyfile
        globus-gass-copy-uninstalled.pc
        globus-gass-copy.pc
        Makefile
	version.h
        test/Makefile
        test/testcred.cnf)
AC_OUTPUT
