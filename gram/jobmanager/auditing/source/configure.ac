AC_PREREQ([2.60])

AC_INIT([globus_gram_audit],[5.0],[https://github.com/gridcf/gct/issues])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [2])
AC_SUBST([PACKAGE_DEPS], [""])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign parallel-tests 1.11 tar-pax])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

AC_ARG_WITH(
    [driver],
    AC_HELP_STRING([--with-driver=DRIVER], [Set name of DBI Driver to use]),
    [AUDIT_DRIVER="$withval"],
    [AUDIT_DRIVER="SQLite"])

AC_SUBST(AUDIT_DRIVER)

AC_ARG_WITH(
    [database],
    AC_HELP_STRING([--with-database=DBNAME], [Set contact string for the database to use]),
    [AUDIT_DATABASE="$withval"],
    [AUDIT_DATABASE=dbname=${localstatedir}/lib/globus/gram-audit-database])
AC_SUBST(AUDIT_DATABASE)

AC_ARG_WITH(
    [username],
    AC_HELP_STRING([--with-username=USERNAME], [Set database username]),
    [AUDIT_USERNAME="$withval"],
    [AUDIT_USERNAME=""])
AC_SUBST(AUDIT_USERNAME)

AC_ARG_WITH(
    [password],
    AC_HELP_STRING([--with-password=PASSWORD], [Set database password]),
    [AUDIT_PASSWORD="$withval"],
    [AUDIT_PASSWORD=""])
AC_SUBST(AUDIT_PASSWORD)

AC_ARG_WITH(
    [schema-version],
    AC_HELP_STRING([--with-schema-version=1|1TG],
        [Choose database schema version]),
    [AUDIT_VERSION="$withval"],
    [AUDIT_VERSION="1TG"])
AC_SUBST(AUDIT_VERSION)

AC_PATH_PROGS([A2X], [a2x a2x.py])
AM_CONDITIONAL(BUILD_MANPAGES, [test "x$A2X" != x])

AC_CONFIG_FILES(
        globus-gram-audit-uninstalled.pc
        globus-gram-audit.pc
        Makefile
        globus-gram-audit
        globus-gram-audit.cron
        gram-audit.conf
        schemas/Makefile)
AC_OUTPUT
