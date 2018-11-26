AC_PREREQ([2.60])

AC_INIT([globus_scheduler_event_generator],[6.1],[https://github.com/gridcf/gct/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [6])
AC_SUBST([PACKAGE_DEPS], ["globus-common >= 14, globus-xio >= 3, globus-gram-protocol >= 11, globus-xio-gsi-driver >= 2"])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])
LT_INIT([dlopen win32-dll])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

PKG_CHECK_MODULES([PACKAGE_DEP], $PACKAGE_DEPS)

AC_PATH_PROGS([DOXYGEN], doxygen)

AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])

AC_ARG_WITH(initscript-config-path,
            AC_HELP_STRING([--with-initscript-config-path=PATH],
            [Set the path to init script configuration. By default, this package
             will install its configuration file in
             ${sysconfdir}/globus-scheduler-event-generator.conf. 
             ]),
            [if test "$withval" = "yes";  then
                AC_MSG_ERROR([Invalid path: $withval])
            else
                SEG_CONFIGDIR=`dirname $withval`
                SEG_CONFIGFILE=`basename $withval`
            fi],
            [
                SEG_CONFIGDIR='${sysconfdir}'
                SEG_CONFIGFILE="globus-scheduler-event-generator.conf"
            ])


AC_SUBST(SEG_CONFIGDIR)
AC_SUBST(SEG_CONFIGFILE)

AC_ARG_WITH(lockfile-path,
            AC_HELP_STRING([--with-lockfile-path=PATH],
            [Set the path to init script lockfile]),
            [if test "$withval" = "yes";  then
                AC_MSG_ERROR([Invalid path: $withval])
            else
                LOCKFILE="$withval"
            fi],
            [
                LOCKFILE="\${localstatedir}/lock/globus-scheduler-event-generator"
            ])
AC_SUBST(LOCKFILE)


LSB="-nolsb"
AC_ARG_WITH(lsb,
        AC_HELP_STRING(
            [--with-lsb],
            [Enable use of LSB support functions in the
             globus-scheduler-event-generator init script. If set, it will use
             linux-specific functions to start
             and stop the globus-scheduler-event-generator service and log its
             status changes. This script will probe for the LSB base scripts if
             this is not set.  If not found, it will use a generic service
             startup script.]),
            [ if test "$withval" = yes; then
                LSB="-lsb"
              fi],
            [
                if test -f /lib/lsb/init-functions; then
                    LSB="-lsb"
                fi
            ])

AC_SUBST(LSB)

AC_ARG_WITH(default-runlevels,
        AC_HELP_STRING(
            [--with-default-runlevels=RUNLEVELS],
            [Choose which System-V style runlevels the
            globus-scheduler-event-generator service will be started on.]),
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


AC_CONFIG_FILES(
        globus-scheduler-event-generator-uninstalled.pc
        globus-scheduler-event-generator.pc
        Makefile
        Doxyfile
        config/Makefile
        init/globus-scheduler-event-generator-nolsb
        init/globus-scheduler-event-generator-lsb
        init/Makefile
        test/Makefile
	version.h)
AC_CONFIG_FILES([globus-scheduler-event-generator-admin],
    [chmod a+x globus-scheduler-event-generator-admin])
AC_OUTPUT
