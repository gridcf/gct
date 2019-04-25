AC_PREREQ([2.60])

AC_INIT([globus_simple_ca],[5.1],[https://github.com/gridcf/gct/issues])
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [2])
AC_SUBST([PACKAGE_DEPS], [""])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax])

m4_include([dirt.sh])
AC_SUBST(DIRT_TIMESTAMP)
AC_SUBST(DIRT_BRANCH_ID)

AC_CANONICAL_HOST

grid_security_conf_tmpl="$srcdir/grid-security.conf.tmpl"
AC_SUBST_FILE(grid_security_conf_tmpl)
ca_signing_policy_tmpl="$srcdir/ca-signing-policy.tmpl"
AC_SUBST_FILE(ca_signing_policy_tmpl)
grid_ca_ssl_conf_tmpl="$srcdir/grid-ca-ssl.conf.tmpl"
AC_SUBST_FILE(grid_ca_ssl_conf_tmpl)

GLOBUS_COMMON_PATH=
PKG_CHECK_EXISTS([globus-common], [
    GLOBUS_COMMON_PATH="$($PKG_CONFIG --variable=path globus-common)"])
AC_SUBST([GLOBUS_COMMON_PATH])
AM_CONDITIONAL([CYGPATH_W_DEFINED], [
    case $host in
        *mingw32*)
        test x"$CYGPATH_W" != x;
        ;;
        *)
        false
        ;;
    esac])


AC_PATH_PROGS(OPENSSL, [openssl])
AC_PATH_PROGS(A2X, [a2x a2x.py])
AM_CONDITIONAL([BUILD_MANPAGES], [test x"$A2X" != x])

AC_CONFIG_FILES(
        [globus-simple-ca-undefined.pc
        globus-simple-ca.pc
        Makefile
        test/Makefile])

AC_CONFIG_FILES([grid-ca-create], [chmod a+x grid-ca-create])
AC_CONFIG_FILES([grid-ca-package], [chmod a+x grid-ca-package])
AC_CONFIG_FILES([grid-ca-sign], [chmod a+x grid-ca-sign])

AC_OUTPUT
