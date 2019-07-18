AC_PREREQ([2.60])

AC_INIT([globus_callout], [4.2], [https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [4])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 15"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_ARG_VAR([LIBLTDL],[libltdl library path])
AC_ARG_VAR([LTDLINCL],[libltdl include header path])
AC_ARG_VAR([LTDLDEPS],[libltdl compile dependencies])

if test x"${LTDLINCL}" != x; then
    CPPFLAGS="${CPPFLAGS} ${LTDLINCL}"
else
    AC_CHECK_HEADERS([ltdl.h],[],[AC_MSG_ERROR([Missing required header ltdl.h])])
fi
if test x"$LIBLTDL" != x; then
    LIBS="${LIBS} ${LIBLTDL}"
else
    AC_CHECK_LIB([ltdl], [lt_dlopen])
fi

AC_PATH_PROGS([DOXYGEN], doxygen)

AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])

AC_CONFIG_FILES(
        Makefile
        globus-callout.pc
        globus-callout-uninstalled.pc
	library/Makefile
	library/Doxyfile
        test/Makefile
	version.h
)
AC_OUTPUT
