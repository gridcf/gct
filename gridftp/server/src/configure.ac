AC_PREREQ([2.60])

AC_INIT([globus_gridftp_server],[13.20],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [7])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 17, globus-xio >= 5, globus-xio-gsi-driver >= 2, globus-gfork >= 3, globus-gridftp-server-control >= 9, globus-ftp-control >= 7, globus-authz >= 2, globus-gssapi-gsi >= 10, globus-gss-assist >= 9, globus-gsi-credential >= 6, globus-gsi-sysconfig >= 5, globus-io >= 9"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])
AC_PROG_CC_C99

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([OPENSSL], [openssl >= 1.0],
    [OPENSSL_PKGCONFIG="openssl >= 1.0"])

AC_SUBST([OPENSSL_PKGCONFIG])
PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

PKG_CHECK_MODULES([ZLIB], [zlib],,
    AC_CHECK_LIB([z], [adler32], [ZLIB_LIBS="-lz"], 
        AC_MSG_ERROR([zlib not found.])))
AC_SUBST([ZLIB_LIBS])

AC_ARG_WITH([preloaded-modules],
    AS_HELP_STRING([--with-preloaded-modules=MODULE,...],
        [Use libtool to preload MODULES when built statically]),
    [if test x"$withval" != x; then
        preload_modules="$withval"
    fi])

if test "x$preload_modules" = x; then
    preload_modules="globus-xio-gsi-driver globus-xio-pipe-driver"
fi

dlpreopen_links=""
dlopen_links=""
link_with_cxx=0
if test x"$preload_modules" != x; then
    preload_modules="$(echo $preload_modules | tr , ' ')"
    for module in $preload_modules; do
        module_found=no
        AC_MSG_CHECKING([for $module module])
        PKG_CHECK_EXISTS([$module], [
            dlopen_links="${dlopen_links:+$dlopen_links }$($PKG_CONFIG --variable=dlopen $module)"
            dlpreopen_links="${dlpreopen_links:+$dlpreopen_links }$($PKG_CONFIG --variable=dlpreopen $module)"
            module_found=yes],
            [module_found=no])
        AC_MSG_RESULT([$module_found])
        if test x"$module" = x"globus-xio-udt-driver"; then
            link_with_cxx=1
        fi
    done
fi

AC_PROG_CXX

AC_SUBST([MODULE_DLPREOPEN], [$dlpreopen_links])
AC_SUBST([MODULE_DLOPEN], [$dlopen_links])

if test "x$link_with_cxx" = x1; then
    AC_MSG_NOTICE([Linking with C++])
fi

AC_PATH_PROGS([OPENSSL], openssl)
AM_CONDITIONAL([ENABLE_TESTS], [test x"$OPENSSL" != x])
AM_CONDITIONAL([LINK_WITH_CXX], [test x"$link_with_cxx" = x1])

AC_MSG_CHECKING([builtin modules])
m4_define([gridftp_modules], m4_normalize(
    m4_esyscmd([ls modules/*/Makefile.am | cut -d/ -f2])))
m4_define([modules_dirs], m4_normalize(m4_foreach(modn, m4_split(gridftp_modules),
    [modules/]modn[ ])))
m4_define([modules_makefiles], 
    m4_normalize(m4_foreach(modd, m4_split(modules_dirs),
        [modd[/Makefile ]])))
m4_define([modules_libs], m4_normalize(
    m4_foreach(modl, m4_split(gridftp_modules),
    [[modules/]modl[/libglobus_gridftp_server_]modl[.la ]])))
m4_define([builtin_extensions_dec], m4_foreach(modl, m4_split(gridftp_modules),
    [[GlobusExtensionDeclareModule(globus_gridftp_server_]modl[);]]))
m4_define([builtin_extensions_def],
    m4_foreach(modl, m4_split(gridftp_modules),
    [[{\"globus_gridftp_server_]modl[\", GlobusExtensionMyModule(globus_gridftp_server_]modl[)},]]))
MODULE_DIRS="gridftp_modules"
MODULE_LIBS="modules_libs"
MODULE_MAKEFILES="modules_makefiles"
BUILTIN_EXTENSIONS_DEC="builtin_extensions_dec"
BUILTIN_EXTENSIONS_DEF="builtin_extensions_def"
AC_MSG_RESULT(gridftp_modules)

if test "X$GRIDMAP" != "X" ; then
    GRIDMAP_CONF="-df $GRIDMAP"
else
    if test -f $HOME/.gridmap ; then
        GRIDMAP_CONF="-df $HOME/.gridmap"
    else
        if test -f /etc/grid-security/grid-mapfile ; then
            GRIDMAP_CONF="-df /etc/grid-security/grid-mapfile"
        else
            GRIDMAP_CONF=""
        fi
    fi
fi

AC_SUBST(GRIDMAP_CONF)
AC_SUBST(GLOBUS_LOCATION)
AC_SUBST(MODULE_DIRS)
AC_SUBST(MODULE_LIBS)
AC_SUBST(BUILTIN_EXTENSIONS_DEC)
AC_SUBST(BUILTIN_EXTENSIONS_DEF)

AC_CHECK_FUNCS(fgetpwent)
AC_FUNC_STRERROR_R
AC_C_BIGENDIAN

AC_PATH_PROGS(PERL, perl perl5)

GLOBUS_COMMON_PATH="$("$PKG_CONFIG" --variable=path globus-common)"
AC_PATH_PROG([GLOBUS_VERSION_PROG], [globus-version], [],
             [$GLOBUS_COMMON_PATH$PATH_SEPARATOR$PATH])
AC_MSG_CHECKING([Toolkit version])
if test "X$GLOBUS_TOOLKIT_ID" = "X"; then
    if test "X$GLOBUS_VERSION" = "X"; then
        if test -x "$GLOBUS_VERSION_PROG"; then
            GLOBUS_TOOLKIT_ID="$("$GLOBUS_VERSION_PROG" -full)"
        else
            GLOBUS_TOOLKIT_ID="unknown"
        fi
    else
        GLOBUS_TOOLKIT_ID="Grid Community Toolkit $GLOBUS_VERSION"
    fi
fi
AC_MSG_RESULT($GLOBUS_TOOLKIT_ID)
AC_SUBST(GLOBUS_TOOLKIT_ID)

reset_exec_prefix_to_none=0
if test x"${exec_prefix}" = xNONE; then
    reset_exec_prefix_to_none=1
    exec_prefix="${prefix}"
fi
eval "conf_sbindir=\"$sbindir\""
eval "conf_bindir=\"$bindir\""
eval "conf_libdir=\"$libdir\""
eval "conf_sysconfdir=\"$sysconfdir\""
eval "conf_localstatedir=\"$localstatedir\""

if test "$reset_exec_prefix_to_none" = 1 ; then
    exec_prefix="NONE"
fi

case "$conf_libdir" in
    /usr/lib|/usr/lib64)
        libdir_line=""
        ;;
    *)
        libdir_line="env += LD_LIBRARY_PATH=${conf_libdir}"
        ;;
esac

AC_SUBST(libdir_line)
AC_SUBST(conf_sbindir)
AC_SUBST(conf_bindir)
AC_SUBST(conf_libdir) 
AC_SUBST(conf_sysconfdir) 
AC_SUBST(conf_localstatedir)

AC_ARG_WITH(default-runlevels,
        AC_HELP_STRING(
            [--with-default-runlevels=RUNLEVELS],
            [Choose which System-V style runlevels the gatekeeper service will
             be started on.]),
            [ case "$withval" in
                yes)
                  AC_MSG_WARN([Invalid parameter to --with-default-runlevels])
                  ;;
                no)
                  CHKCONFIG_DEFAULT="-"
                  DEFAULT_START=""
                  DEFAULT_STOP=""
                  ;;
                *)
                  CHKCONFIG_DEFAULT="$withval"
                  for r in 0 1 2 3 4 5 6; do
                      if expr $withval : ".*$r.*" > /dev/null; then
                          DEFAULT_START="${DEFAULT_START:+$DEFAULT_START }$r"
                      else
                          DEFAULT_STOP="${DEFAULT_STOP:+$DEFAULT_STOP }$r"
                      fi
                  done
              esac],
            [
                CHKCONFIG_DEFAULT="-"
                DEFAULT_START=""
                DEFAULT_STOP=""
            ])

if test "$DEFAULT_START" = ""; then
    DEFAULT_START_LINE="#"
else
    DEFAULT_START_LINE="# Default-Start:     $DEFAULT_START"
fi
if test "$DEFAULT_STOP" = ""; then
    DEFAULT_STOP_LINE="#"
else
    DEFAULT_STOP_LINE="# Default-Stop:      $DEFAULT_STOP"
fi

AC_SUBST(CHKCONFIG_DEFAULT)
AC_SUBST(DEFAULT_START_LINE)
AC_SUBST(DEFAULT_STOP_LINE)

AC_PATH_PROG(FAKEROOT, "fakeroot")
AM_CONDITIONAL(TEST_FAKEROOT, test -n "$FAKEROOT")


AC_PATH_PROGS([A2X], [a2x a2x.py])
AM_CONDITIONAL(BUILD_MANPAGES, [test "x$A2X" != x])

AC_CONFIG_HEADERS(globus_i_gridftp_server_config.h)

AC_CONFIG_FILES(
    globus-gridftp-server.pc
    globus-gridftp-server-uninstalled.pc
    globus-gridftp-password
    globus-gridftp-server-enable-sshftp
    Makefile
    modules_makefiles
    modules/Makefile
    init/Makefile
    init/globus-gridftp-server
    init/globus-gridftp-sshftp
    gridftp.conf.default
    gridftp.xinetd.default
    gridftp.gfork.default
    test/Makefile
    test/testcred.cnf
    version.h
    extensions.h)

AC_OUTPUT
