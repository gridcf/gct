AC_PREREQ([2.60])

AC_INIT([globus_gsi_proxy_core],[9.4],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [9])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-openssl-module >= 3, globus-gsi-openssl-error >= 2, globus-gsi-cert-utils >= 8, globus-gsi-sysconfig >= 5, globus-gsi-proxy-ssl >= 4, globus-gsi-credential >= 5"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
AC_USE_SYSTEM_EXTENSIONS
LT_INIT([dlopen win32-dll])


AM_PROG_CC_C_O
AC_PROG_CC_C99

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([OPENSSL], [openssl >= 1.0],
    [OPENSSL_PKGCONFIG="openssl >= 1.0"])

AC_SUBST([OPENSSL_PKGCONFIG])
PKG_CHECK_MODULES([PACKAGE_DEP], [$PACKAGE_DEPS])

AC_ARG_WITH([symbol-versions],
    AC_HELP_STRING(
        [--with-symbol-versions],
        [Enable binary-backward symbol versions]),
    [case "$host" in
    *-*-linux* | *-*-gnu* | *-*-k*bsd*-gnu)
        if test "$withval" = yes; then
            AC_MSG_NOTICE([Using backward-compatibility symbol versions])
            use_symbol_versions="yes"
        fi
        ;;
    *)
        if test "$withval" = yes; then
            AC_MSG_NOTICE([Ignoring --with-symbol-versions on unsupported operating system/linker])
        fi
    esac],
        [use_symbol_versions="no"])

AM_CONDITIONAL(USE_SYMBOL_VERSIONS, test "$use_symbol_versions" = "yes")

if test "$use_symbol_versions" = yes; then
    AC_DEFINE(USE_SYMBOL_VERSIONS, 1, [Use backward-compatibility symbol versioning])
fi


AC_PATH_PROGS([DOXYGEN], doxygen)
AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])

AC_OUTPUT(
        globus-gsi-proxy-core.pc
        globus-gsi-proxy-core-uninstalled.pc
        Makefile
	library/Makefile
        library/Doxyfile
        test/Makefile
	version.h
)
