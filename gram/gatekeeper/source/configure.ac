AC_PREREQ([2.60])

AC_INIT([globus_gatekeeper],[11.3],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [3])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-gss-assist >= 8, globus-gssapi-gsi >= 9"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

AC_CONFIG_HEADERS(globus_gatekeeper_config.h)

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([OPENSSL], [openssl >= 1.0],
    [OPENSSL_PKGCONFIG="openssl >= 1.0"])

AC_SUBST([OPENSSL_PKGCONFIG])
PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_CHECK_HEADERS(proj.h)
AC_CHECK_HEADERS(netinet/tcp.h)
AC_CHECK_HEADERS(string.h)
AC_CHECK_HEADERS(strings.h)
AC_CHECK_HEADERS(malloc.h)
AC_CHECK_HEADERS(netinet/in/system.h)
AC_CHECK_HEADERS(netinet/in/systm.h)
AC_CHECK_HEADERS(netinet/ip.h)
AC_CHECK_HEADERS(netinet/tcp.h)
AC_CHECK_HEADERS(paths.h)
AC_CHECK_HEADERS(sys/resource.h)
AC_CHECK_HEADERS(sys/select.h)
AC_CHECK_HEADERS(sys/time.h)
AC_CHECK_HEADERS(unistd.h)
AC_HEADER_DIRENT

AC_HEADER_SYS_WAIT
AC_CHECK_FUNCS(waitpid)
AC_CHECK_FUNCS(wait3)
AC_HEADER_STDC

AC_ARG_WITH(initscript-config-path,
            AC_HELP_STRING([--with-initscript-config-path=PATH],
            [Set the path to init script configuration. By default, this package
             will install its configuration file in
             ${sysconfdir}/globus-gatekeeper.conf. 
             ]),
            [if test "$withval" = "yes";  then
                AC_MSG_ERROR([Invalid path: $withval])
            else
                GATEKEEPER_CONFIGDIR=`dirname $withval`
                GATEKEEPER_CONFIGFILE=`basename $withval`
            fi],
            [
                GATEKEEPER_CONFIGDIR='${sysconfdir}'
                GATEKEEPER_CONFIGFILE="globus-gatekeeper.conf"
            ])

AC_ARG_WITH(lockfile-path,
            AC_HELP_STRING([--with-lockfile-path=PATH],
            [Set the path to init script lockfile]),
            [if test "$withval" = "yes";  then
                AC_MSG_ERROR([Invalid path: $withval])
            else
                LOCKFILE="$withval"
            fi],
            [
                LOCKFILE="\${localstatedir}/lock/globus-gatekeeper"
            ])
AC_SUBST(LOCKFILE)

LSB="-nolsb"
AC_ARG_WITH(lsb,
        AC_HELP_STRING(
            [--with-lsb],
            [Enable use of LSB support functions in the globus-gatekeeper
            init script. If set, it will use linux-specific functions to start
            and stop the gatekeeper service and log its status changes. This
            script will probe for the LSB base scripts if this is not set.
            If not found, it will use a generic service startup script.]),
            [ if test "$withval" = yes; then
                LSB="-lsb"
              fi],
            [
                if test -f /lib/lsb/init-functions; then
                    LSB="-lsb"
                fi
            ])

AC_SUBST(LSB)
AC_SUBST(GATEKEEPER_CONFIGDIR)
AC_SUBST(GATEKEEPER_CONFIGFILE)
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

disable_gatekeeper=no
case $host in
    *mingw* | *win32*)
        disable_gatekeeper=yes
        ;;
esac
AM_CONDITIONAL([BUILD_GATEKEEPER], test "X$disable_gatekeeper" != Xyes)

AC_SUBST(CHKCONFIG_DEFAULT)
AC_SUBST(DEFAULT_START_LINE)
AC_SUBST(DEFAULT_STOP_LINE)
AC_SUBST(LSB)

AC_PATH_PROGS([A2X], [a2x a2x.py])
AM_CONDITIONAL(BUILD_MANPAGES, [test "x$A2X" != x])

AC_CONFIG_FILES(
        globus-gatekeeper-uninstalled.pc
        globus-gatekeeper.pc
        init/Makefile
        init/globus-gatekeeper-lsb
        init/globus-gatekeeper-nolsb
        config/Makefile
        config/globus-gatekeeper
        config/$GATEKEEPER_CONFIGFILE:config/globus-gatekeeper.in
        Makefile
)
AC_OUTPUT
