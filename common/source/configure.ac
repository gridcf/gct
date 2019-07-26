AC_PREREQ([2.60])
AC_INIT([globus_common], [18.5], [https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [18])
AC_SUBST([PACKAGE_DEPS], [""])

AC_CONFIG_AUX_DIR([build-aux])

AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])
AM_PROG_CC_C_O
AC_PROG_CC_C99
AC_PROG_MKDIR_P

AC_CONFIG_HEADERS(globus_i_common_config.h)

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

AC_SYS_LARGEFILE
if test X"$ac_cv_sys_largefile_CC" != "Xno"; then
    LARGEFILE_CFLAGS="$ac_cv_sys_largefile_CC"
    AC_SUBST(LARGEFILE_CFLAGS)
fi
if test X"$ac_cv_sys_file_offset_bits" != "Xno" && \
    test X"$ac_cv_sys_file_offset_bits" != "Xunknown"; then
    LARGEFILE_CFLAGS="${LARGEFILE_CFLAGS} -D_FILE_OFFSET_BITS=$ac_cv_sys_file_offset_bits" 
fi

if test X"$ac_cv_sys_large_files" = "X1"; then
    LARGEFILE_CFLAGS="${LARGEFILE_CFLAGS} -D_LARGE_FILES=$ac_cv_sys_large_files" 
fi
AC_SUBST(LARGEFILE_CFLAGS)

LAC_THREADS
CHECK_REENTRANT_FUNCS

LT_LIB_DLLOAD
AC_SEARCH_LIBS(socket, socket)
AC_SEARCH_LIBS(gethostbyname, nsl)
AC_SEARCH_LIBS(gethostbyaddr, nsl)

PACKAGE_DEP_CFLAGS=""
PACKAGE_DEP_LIBS=""

BUILD_FOR_WINDOWS=no
case $host in
    *mingw* | *win32*)
        PACKAGE_DEP_LIBS="$PACKAGE_DEP_LIBS -lws2_32 -liphlpapi"
        PACKAGE_DEP_CFLAGS="-DWINVER=0x0502"
        BUILD_FOR_WINDOWS=yes
        ac_cv_func_gethostname=yes
        ;;
esac

gtsave_LIBS="$LIBS"
gtsave_CPPFLAGS="$CPPFLAGS"
LIBS="$LIBS $PACKAGE_DEP_LIBS"
CPPFLAGS="$CPPFLAGS -DWINVER=0x0502"
AC_CHECK_HEADERS([arpa/inet.h])
AC_CHECK_HEADERS([ifaddrs.h])
AC_CHECK_HEADERS([net/if_arp.h])
AC_CHECK_HEADERS([net/if_dl.h])
AC_CHECK_HEADERS([sys/socket.h])
AC_CHECK_HEADERS([net/if.h],[],[],[#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif])
AC_CHECK_HEADERS([sys/ioctl.h])
AC_CHECK_HEADERS([ifaddrs.h])
AC_CHECK_HEADERS([if/dl.h])
AC_CHECK_HEADERS([if/arp.h])
AC_CHECK_HEADERS([arpa/inet.h])
AC_CHECK_HEADERS([strings.h])
AC_CHECK_HEADERS([syslog.h])
AC_CHECK_HEADERS([sys/sysctl.h])
AC_CHECK_HEADERS([sys/types.h])
AC_DIR_HEADER

AC_SUBST(PACKAGE_DEP_CFLAGS)
AC_SUBST(PACKAGE_DEP_LIBS)

AC_CHECK_FUNCS(lt_dlmutex_register)
AC_CHECK_FUNCS([gethostname])
AC_CHECK_FUNCS([geteuid])
AC_CHECK_FUNCS([fork])
AC_CHECK_FUNCS([seekdir])
AC_CHECK_FUNCS([telldir])
AC_CHECK_FUNCS([sigaction])
AC_CHECK_FUNCS([sigprocmask])
AC_CHECK_FUNCS([snprintf])
AC_CHECK_FUNCS([vsnprintf])
AC_CHECK_FUNCS([strncasecmp])
AC_PATH_PROG([DOXYGEN], doxygen)
LIBS="$gtsave_LIBS"
CPPFLAGS="$gtsave_CPPFLAGS"

AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])

m4_include(globus_version)
AC_SUBST(GLOBUS_VERSION)
AC_SUBST(GLOBUS_VERSIONNAME)

AC_PATH_PROGS(PERL, perl perl5)
AC_PATH_PROGS(SH, sh bash ksh)

AC_ARG_WITH([backward-compatibility-hack],
    AC_HELP_STRING(
        [--with-backward-compatibility-hack],
        [Enable binary-backward compatibility with GT 5.0.x by adding symbol
         aliases for different library versions]),
    [case "$host" in
    *-*-linux* | *-*-gnu* | *-*-k*bsd*-gnu)
        if test "$withval" = yes; then
            AC_MSG_NOTICE([Using backward-compatibility symbol labels])
            use_symbol_labels="yes"
        fi
        ;;
    *)
        if test "$withval" = yes; then
            AC_MSG_NOTICE([Ignoring --with-backward-compatibility-hack on unsupported operating system/linker])
        fi
    esac],
        [use_symbol_labels="no"])

AC_ARG_WITH([perlmoduledir],
AC_HELP_STRING([--with-perlmoduledir=DIR],
[perl module directory [[PREFIX/lib/perl]]]),
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

AM_CONDITIONAL(USE_SYMBOL_LABELS, test "$use_symbol_labels" = "yes")

if test "$use_symbol_labels" = yes; then
    AC_DEFINE(USE_SYMBOL_LABELS, 1, [Use backward-compatibility symbol labels])
fi

AM_CONDITIONAL(USES_LTO, [echo $LDFLAGS | grep -q -- -flto])

AC_PATH_PROGS([A2X], [a2x a2x.py])
AM_CONDITIONAL(BUILD_MANPAGES, [test x"$A2X" != x])

AC_SUBST([cross_compiling])
AM_CONDITIONAL(CROSS_COMPILING, [ test "$cross_compiling" = "yes" ])
AM_CONDITIONAL(WINDOWS_TEST_SCRIPT, [ test "$BUILD_FOR_WINDOWS" = "yes"])

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


AC_CONFIG_FILES([
	Makefile
        globus-common.pc
        globus-common-uninstalled.pc
	library/Makefile
	library/Doxyfile
        library/globus_l_common_paths.h
	programs/Makefile
        programs/globus-sh-exec
	scripts/Makefile
        scripts/Paths.pm
        scripts/globus-sh-tools.sh
        scripts/globus-sh-tools-vars.sh
        scripts/globus-script-initializer
        scripts/globus-user-env.csh
        scripts/globus-user-env.sh
        scripts/globus-script-initializer.$host_cpu-$host_vendor-$host_os:scripts/globus-script-initializer-host.in
        test/Makefile
        manpages/Makefile
	version.h])
AC_CONFIG_FILES([programs/globus-version], [chmod a+x programs/globus-version])
AC_CONFIG_FILES([scripts/globus-domainname:scripts/globus-hostname.in],
        [chmod a+x scripts/globus-domainname])
AC_CONFIG_FILES([scripts/globus-hostname],
        [chmod a+x scripts/globus-hostname])
AC_CONFIG_FILES([scripts/globus-makefile-header],
        [chmod a+x scripts/globus-makefile-header])
AC_OUTPUT
