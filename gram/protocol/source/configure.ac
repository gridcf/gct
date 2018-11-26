AC_PREREQ([2.60])

AC_INIT([globus_gram_protocol],[13.2],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [10])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-io >= 8, globus-gssapi-gsi >= 10, globus-gss-assist >= 8"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

AC_PROG_MKDIR_P

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

case $build:$host in
    *cygwin*:*mingw32*)
        use_cygpath_w=yes
        ;;
    *)
        use_cygpath_w=no
        ;;
esac
AM_CONDITIONAL([CYGPATH_W_DEFINED], [test yes = $use_cygpath_w])

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)
PKG_CHECK_EXISTS([globus-xio-gsi-driver >= 2], [
    GSI_DRIVER_CFLAGS="$($PKG_CONFIG --cflags globus-xio-gsi-driver)"
    GSI_DRIVER_LIBS="$($PKG_CONFIG --variable=dlopen globus-xio-gsi-driver)"])
if test x"$GSI_DRIVER_LIBS" = x; then
    DLPREOPEN_FORCE="-dlpreopen force"
fi
AC_SUBST([GSI_DRIVER_CFLAGS])
AC_SUBST([GSI_DRIVER_LIBS])
AC_SUBST([DLPREOPEN_FORCE])

AC_PATH_PROGS([OPENSSL], openssl)
AM_CONDITIONAL([ENABLE_TESTS], [test "x$OPENSSL" != x])
AC_PATH_PROGS([PERL], perl perl5)

AC_ARG_WITH([perlmoduledir],
AC_HELP_STRING([--with-perlmoduledir=DIR],
[perl module directory [PREFIX/lib/perl]]),
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

AC_PATH_PROGS([DOXYGEN], doxygen)

AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])
AC_CONFIG_FILES(
        globus-gram-protocol-uninstalled.pc
        globus-gram-protocol.pc
        Makefile
        scripts/Makefile
        test/Makefile
        test/testcred.cnf
        Doxyfile
	version.h)

AC_OUTPUT
