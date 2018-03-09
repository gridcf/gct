AC_PREREQ([2.60])

AC_INIT([globus_gsi_openssl_error], [3.8],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [3])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([OPENSSL], [openssl >= 1.0],
    [OPENSSL_PKGCONFIG="openssl >= 1.0"],
    [
        PKG_CHECK_MODULES([OPENSSL], [openssl101e])
        OPENSSL_PKGCONFIG="openssl101e"
    ])

AC_SUBST([OPENSSL_PKGCONFIG])
PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_PATH_PROGS([DOXYGEN], doxygen)
AM_CONDITIONAL([ENABLE_DOXYGEN], [test "x$DOXYGEN" != x])

AC_CONFIG_FILES(
        Makefile
        globus-gsi-openssl-error.pc
        globus-gsi-openssl-error-uninstalled.pc
        library/Makefile
	library/Doxyfile
        test/Makefile
        version.h
)
AC_OUTPUT
