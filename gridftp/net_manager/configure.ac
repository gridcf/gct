AC_PREREQ([2.60])

AC_INIT([globus_net_manager],[1.4],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [1])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 15.27, globus-xio >= 5"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

AC_PROG_CC_C99

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_PATH_PROGS([DOXYGEN], doxygen)

AC_ARG_ENABLE([python],
    AS_HELP_STRING([--enable-python],[Enable Python Module]),
[
if test "$enableval" = yes; then
    AC_PATH_PROGS([PYTHON_CONFIG], [python-config python2.6-config])
    if test x"$PYTHON_CONFIG" != x; then
        PYTHON_CPPFLAGS="$CPPFLAGS $($PYTHON_CONFIG --includes)"
        if $PYTHON_CONFIG --embed >/dev/null 2>&1; then
            PYTHON_LIBS="$LIBS $($PYTHON_CONFIG --libs --embed)"
        else
            PYTHON_LIBS="$LIBS $($PYTHON_CONFIG --libs)"
        fi
    else
        AC_MSG_ERROR([Cannot find usable python-config])
    fi
fi
])
AC_SUBST(PYTHON_CPPFLAGS)
AC_SUBST(PYTHON_LIBS)

AC_ARG_VAR([LIBLTDL],[libltdl library path])
AC_ARG_VAR([LTDLDEPS],[libltdl compile dependencies])

if test x"$LIBLTDL" = x; then
    save_libs="$LIBS"
    AC_CHECK_LIB([ltdl], [lt_dlopen], [LIBLTDL="-lltdl"])
    LIBS="$save_libs"
fi
AC_SUBST([LIBLTDL])

if test "x$PYTHON_CONFIG" != x; then
    AC_DEFINE([ENABLE_PYTHON], [1], [Define to `1' to build python module])
fi
AM_CONDITIONAL([ENABLE_PYTHON], [test "x$PYTHON_CONFIG" != x])
AM_CONDITIONAL([ENABLE_DOXYGEN], [test "x$DOXYGEN" != "x"])

AC_CONFIG_FILES(
        globus-net-manager-uninstalled.pc
        globus-net-manager.pc
        Makefile
        Doxyfile
        attr/Makefile
        context/Makefile
        logging/Makefile
        python/Makefile
        test/Makefile
        xio_driver/Makefile
        xio_driver/globus-xio-net-manager-driver-uninstalled.pc
        xio_driver/globus-xio-net-manager-driver.pc
	version.h)
AC_OUTPUT
