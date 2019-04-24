AC_PREREQ([2.60])

AC_INIT([globus_xio], [6.2], [https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST(MAJOR_VERSION, [${PACKAGE_VERSION%%.*}])
AC_SUBST(MINOR_VERSION, [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [6])
AC_SUBST(PACKAGE_DEPS, ["globus-common >= 14"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

m4_include(dirt.sh)
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

AC_CHECK_HEADERS([netinet/tcp.h])
PKG_CHECK_MODULES([GLOBUS_COMMON], $PACKAGE_DEPS)
PACKAGE_DEP_CFLAGS="$GLOBUS_COMMON_CFLAGS"
PACKAGE_DEP_LIBS="$GLOBUS_COMMON_LIBS"
AC_SUBST(PACKAGE_DEP_CFLAGS)
AC_SUBST(PACKAGE_DEP_LIBS)

dnl Build hooks for all 'builtin' drivers
AC_MSG_CHECKING(builtin drivers)
XIO_BUILTIN_DIRS=
XIO_BUILTIN_LIBS=
BUILTIN_EXTENSIONS_DEC=
BUILTIN_EXTENSIONS_DEF=
m4_define([builtin_dirs],dnl
m4_normalize(m4_esyscmd([ls builtins/*/Makefile.am | cut -d/ -f2])))
m4_define([builtin_makefiles],dnl
m4_normalize(m4_foreach([ext], m4_split(builtin_dirs), [builtins/]ext[/Makefile ])))

XIO_BUILTIN_DIRS_MACRO="builtin_dirs"
XIO_BUILTIN_PC_INCLUDES="m4_foreach([d], m4_split(builtin_dirs), [ -I\${abs_top_srcdir}/builtins/]d)"
for builtin in $XIO_BUILTIN_DIRS_MACRO; do
      XIO_BUILTIN_DIRS="${XIO_BUILTIN_DIRS} $builtin"
      lib_name="builtins/${builtin}/libglobus_xio_${builtin}_driver.la"
      XIO_BUILTIN_LIBS="$XIO_BUILTIN_LIBS $lib_name"
    
      BUILTIN_EXTENSIONS_DEC="${BUILTIN_EXTENSIONS_DEC} GlobusXIODeclareModule(${builtin});"
      BUILTIN_EXTENSIONS_DEF="${BUILTIN_EXTENSIONS_DEF} {GlobusXIOExtensionName(${builtin}), GlobusXIOMyModule(${builtin})},"
done
AC_MSG_RESULT([$XIO_BUILTIN_DIRS])

AC_SUBST(XIO_BUILTIN_DIRS)
AC_SUBST(XIO_BUILTIN_LIBS)
AC_SUBST(BUILTIN_EXTENSIONS_DEC)
AC_SUBST(BUILTIN_EXTENSIONS_DEF)
AC_SUBST(XIO_BUILTIN_PC_INCLUDES)

AC_CONFIG_HEADERS([globus_i_xio_config.h])

AC_CHECK_FUNCS(sysconf)
AC_CHECK_FUNCS(readv)
AC_CHECK_FUNCS(writev)
AC_CHECK_FUNCS(recvmsg)
AC_CHECK_FUNCS(sendmsg)

if test "$exec_prefix" = NONE; then
    reset_exec_prefix_to_none=1
    exec_prefix="$prefix"
fi
eval "eval_libexecdir=$libexecdir"
if test "$reset_exec_prefix_to_none" = 1 ; then
    exec_prefix=NONE
fi

AC_PATH_PROGS([DOXYGEN], doxygen)

AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])

AC_CONFIG_FILES(
    Makefile
    globus-xio.pc
    globus-xio-uninstalled.pc
    builtins/Makefile
    test/Makefile
    test/drivers/Makefile
    Doxyfile
    version.h
    extensions.h
    builtin_makefiles)
AC_OUTPUT
