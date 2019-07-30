AC_PREREQ([2.60])

AC_INIT([globus_io],[12.2],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [9])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-xio >= 3, globus-gss-assist >= 8, globus-gssapi-gsi >= 10, globus-xio-gsi-driver >= 2, globus-gssapi-error >= 4"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

AC_SUBST([LIBLTDL])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

AM_CONDITIONAL([CYGPATH_W_DEFINED], [
    case $host in
        *mingw32*)
        test x"$CYGPATH_W" != x;
        ;;
        *)
        false
        ;;
    esac])

AC_PATH_PROGS([OPENSSL], openssl)

AM_CONDITIONAL([ENABLE_TESTS], [test x"$OPENSSL" != x])

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

PKG_CHECK_EXISTS([globus-xio-gsi-driver >= 2], [
    GSI_DRIVER_CFLAGS="$($PKG_CONFIG --cflags globus-xio-gsi-driver)"
    GSI_DRIVER_DLPREOPEN="$($PKG_CONFIG --variable=dlpreopen globus-xio-gsi-driver)"])

AC_SUBST([GSI_DRIVER_CFLAGS])
AC_SUBST([GSI_DRIVER_DLPREOPEN])

AC_CONFIG_FILES(
        globus-io.pc
        globus-io-uninstalled.pc
	Makefile
        test/Makefile
	version.h
	test/testcred.cnf
)

AC_OUTPUT
