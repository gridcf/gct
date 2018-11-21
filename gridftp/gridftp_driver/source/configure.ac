AC_PREREQ([2.60])

AC_INIT([globus_xio_gridftp_driver],[3.2],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [2])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-xio >= 3, globus-ftp-client >= 7, globus-xio-gsi-driver >= 2"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

PKG_CHECK_EXISTS([globus-xio-gsi-driver], [
    GSI_DRIVER_DLOPEN="$($PKG_CONFIG --variable=dlopen globus-xio-gsi-driver)"
    GSI_DRIVER_DLPREOPEN="$($PKG_CONFIG --variable=dlpreopen globus-xio-gsi-driver)"])
AC_SUBST([GSI_DRIVER_DLOPEN])
AC_SUBST([GSI_DRIVER_DLPREOPEN])

PKG_CHECK_EXISTS([globus-xio-pipe-driver], [
    PIPE_DRIVER_DLOPEN="$($PKG_CONFIG --variable=dlopen globus-xio-pipe-driver)"])
AC_SUBST([PIPE_DRIVER_DLOPEN])

PKG_CHECK_EXISTS([globus-gridftp-server], [
    GRIDFTP_PATH="$($PKG_CONFIG --variable=path globus-gridftp-server)"
    GRIDFTP_EXE="$($PKG_CONFIG --variable=executable globus-gridftp-server)"])
AC_SUBST(GRIDFTP_PATH)
AC_SUBST(GRIDFTP_EXE)

AC_PATH_PROGS([OPENSSL], openssl)
AM_CONDITIONAL([ENABLE_TESTS], [test "x$OPENSSL" != x])

AC_PATH_PROGS([DOXYGEN], doxygen)
AM_CONDITIONAL([ENABLE_DOXYGEN], [test "x$DOXYGEN" != x])

AC_CONFIG_FILES(
        globus-xio-gridftp-driver-uninstalled.pc
        globus-xio-gridftp-driver.pc
        Makefile
        Doxyfile
	version.h
        test/Makefile
        test/testcred.cnf)
AC_OUTPUT
