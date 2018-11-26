AC_PREREQ([2.60])

AC_INIT([globus_ftp_client],[9.2],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [7])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 15, globus-ftp-control >= 4, globus-gsi-callback >= 4, globus-gsi-credential >= 5, globus-gsi-sysconfig >= 5, globus-gssapi-gsi >= 10, globus-xio >= 3, globus-xio-popen-driver >= 2"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign parallel-tests 1.11 tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

GLOBUS_COMMON_PATH=
PKG_CHECK_EXISTS([globus-common], [
    GLOBUS_COMMON_PATH="$($PKG_CONFIG --variable=path globus-common)"])
AC_SUBST([GLOBUS_COMMON_PATH])

if test x"$GLOBUS_COMMON_PATH" != ""; then
    PATH="${GLOBUS_COMMON_PATH}:${PATH}"
fi
AC_MSG_CHECKING([Toolkit version])
if test "X$GLOBUS_TOOLKIT_ID" = "X"; then
    if test "X$GLOBUS_VERSION" = "X"; then
        if command -v globus-version > /dev/null; then
            GLOBUS_TOOLKIT_ID="`globus-version -full`"
        else
            GLOBUS_TOOLKIT_ID="unknown"
        fi
    else
        GLOBUS_TOOLKIT_ID="Grid Community Toolkit $GLOBUS_VERSION"
    fi
fi
AC_MSG_RESULT($GLOBUS_TOOLKIT_ID)
AC_SUBST(GLOBUS_TOOLKIT_ID)

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

AM_CONDITIONAL([TARGET_MINGW], [
    case $host in
        *mingw*)
            true
            ;;
        *)
            false
            ;;
    esac])

AM_CONDITIONAL([TARGET_HURD], [
    case $host in
        *-linux*|*-k*bsd-gnu*)
            false
            ;;
        *-gnu*)
            true
            ;;
        *)
            false
            ;;
    esac])


GRIDFTP_SERVER_PATH=`$PKG_CONFIG --variable=path globus-gridftp-server`
GRIDFTP_SERVER_EXE=`$PKG_CONFIG --variable=executable globus-gridftp-server`
AC_SUBST(GRIDFTP_SERVER_PATH)
AC_SUBST(GRIDFTP_SERVER_EXE)

PKG_CHECK_MODULES([OPENSSL], [openssl >= 1.0],
    [OPENSSL_PKGCONFIG="openssl >= 1.0"])

AC_SUBST([OPENSSL_PKGCONFIG])

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)
PKG_CHECK_EXISTS([globus-xio-gsi-driver >= 2], [
    GSI_DRIVER_CFLAGS="$($PKG_CONFIG --cflags globus-xio-gsi-driver)"
    GSI_DRIVER_DLOPEN="$($PKG_CONFIG --variable=dlopen globus-xio-gsi-driver)"
    GSI_DRIVER_DLPREOPEN="$($PKG_CONFIG --variable=dlpreopen globus-xio-gsi-driver)"])
PKG_CHECK_EXISTS([globus-xio-pipe-driver >= 2], [
    PIPE_DRIVER_DLOPEN="$($PKG_CONFIG --variable=dlopen globus-xio-pipe-driver)"
    PIPE_DRIVER_DLPREOPEN="$($PKG_CONFIG --variable=dlpreopen globus-xio-pipe-driver)"])
AC_SUBST([GSI_DRIVER_CFLAGS])
AC_SUBST([GSI_DRIVER_DLOPEN])
AC_SUBST([GSI_DRIVER_DLPREOPEN])
AC_SUBST([PIPE_DRIVER_LIBS])
AC_SUBST([PIPE_DRIVER_DLOPEN])
AC_SUBST([PIPE_DRIVER_DLPREOPEN])

AC_PATH_PROGS([OPENSSL], openssl)
AM_CONDITIONAL([ENABLE_TESTS], [test x"$OPENSSL" != x])
AC_PATH_PROGS([SSH_BIN], ssh, "/usr/bin/ssh") 

AC_PATH_PROGS([DOXYGEN], doxygen)

AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])
AC_CONFIG_FILES(
        globus-ftp-client-uninstalled.pc
        globus-ftp-client.pc
        gridftp-ssh
        Makefile
        Doxyfile
        test/Makefile
	version.h
        test/testcred.cnf)
AC_OUTPUT
