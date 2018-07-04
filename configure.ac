m4_define([globus_buildno], [m4_syscmd([
if test -e .git ; then
    git log -n 1 --pretty=format:%ct > globus-version.inc.new;
    if ! cmp globus-version.inc.new globus-version.inc > /dev/null 2>&1; then
        mv globus-version.inc.new globus-version.inc
    else
        rm globus-version.inc.new
    fi
fi])dnl
m4_[]include(globus-version.inc)])dnl
m4_define([gtreleasebuild], [6.0.]globus_buildno)
AC_INIT([gct], gtreleasebuild, [https://github.com/gridcf/gct/issues])

m4_define([short_version], [m4_substr(AC_PACKAGE_VERSION, [0],
    m4_index(AC_PACKAGE_VERSION, [.]))])
AC_PREFIX_DEFAULT([/usr/local/globus-]short_version)
export GLOBUS_VERSION=$VERSION
syscmd([./update-dirt.sh])
syscmd([test -x ./prep-gsissh && sh -x ./prep-gsissh || true])
syscmd([test -x ./write-globus-version && sh -x ./write-globus-version ]AC_PACKAGE_VERSION)
m4_include([gsi_openssh/version.m4])
AC_SUBST([GSI_OPENSSH_VERSION], [gsissh_version])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests tar-pax subdir-objects])
AC_CONFIG_HEADERS([ltdlconfig.h])
LT_CONFIG_LTDL_DIR([libltdl])
LT_INIT([dlopen win32-dll])
LTDL_INIT([recursive installable])
AC_CONFIG_FILES([libltdl/Makefile])

AM_PROG_CC_C_O
AC_PROG_CC_C99

AC_MSG_CHECKING([for need to compile ltdl])
AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM([[int have_ltdl=HAVE_LTDL;]],
                         [[return have_ltdl;]])],
        [AC_MSG_RESULT([no])
        HAVE_LTDL=1],
        [AC_MSG_RESULT([yes])
        HAVE_LTDL=0])

AM_CONDITIONAL([HAVE_LTDL], [test "$HAVE_LTDL" = 1])
AC_SUBST([LTDL_VERSION], LT_PACKAGE_VERSION)

expand_libtool_var()
{
    varname="$1"
    eval "value=\${$varname}"
    value="$(echo "$value" | tr '()' '{}')"
    top_build_prefix=${top_build_prefix:-$(pwd)/}
    top_builddir=${top_builddir:-$(pwd)}
    if test "x${top_srcdir}" = "x"; then
        top_srcdir="${srcdir}"
    fi
    if test x"${top_srcdir#/}" = x"${top_srcdir}"; then
        top_srcdir="$(pwd)/${top_srcdir}"
    else
        top_srcdir="${top_srcdir}"
    fi
    eval "echo $varname=$value"
}
eval `expand_libtool_var LIBLTDL`
eval `expand_libtool_var LTDLDEPS`
eval `expand_libtool_var INCLTDL`
eval `expand_libtool_var LTDLINCL`
export LIBLTDL LTDLDEPS LTDLINCL INCLTDL

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])

dnl Leading newline allows this multi-line value be AC_SUBST into the
dnl Makefile with a dummy first macro created by the automatic var = @var@
dnl that automake generates
m4_define([TARGETS_MACRO],[
])
# GT_PACKAGE(package, subdir, dependencies, bundle, is_xio_driver)
m4_define([GT_PACKAGE],
[
ifelse([$1], [gsi-openssh], [m4_define([pkgver], gsissh_version)], [
m4_pushdef([AC_INIT],[$]2)dnl
m4_define([pkgver], m4_normalize(m4_esyscmd(grep AC[_]INIT [$2][/configure.ac])))dnl
m4_popdef([AC_INIT])dnl
])
m4_define([native_name], m4_translit([$1],[_],[-]))
m4_define([underscored_name], m4_translit([$1],[-],[_]))

    m4_append([TARGETS_MACRO],[dnl
$1-stamp_SUBDIR = $2
$1-all_SUBDIR = $2
$1-clean_SUBDIR = $2
$1-install_SUBDIR = $2
$1-check_SUBDIR = $2
$1-check: $1-stamp
$1-clean_stamp = $1-stamp
$1-install_stamp = $1-stamp
$1-dist_SUBDIR = $2
$1-deps = m4_foreach(p, m4_split(m4_normalize($3)), p[-stamp ]) 
$1: $1-all
$1-all: $1-stamp
$1-install: $1-stamp
$1-stamp: [\$(]$1[-deps)]
$1-dist-tarball = $1-]pkgver[.tar.gz
package-output/\$($1-dist-tarball): $1-dist
$1-rpm_SPECFILE = \$(srcdir)/packaging/fedora/]native_name[.spec
$1-rpm_SOURCE = package-output/\$($1-dist-tarball)
$1-rpm: \$($1-rpm_SOURCE) \$($1-rpm_SPECFILE)
$1-deb_PACKAGE_NAME = ]native_name[
$1-deb_SOURCE = package-output/\$($1-dist-tarball)
$1-deb_TARBALL = debbuild/]native_name[_]m4_translit(pkgver,[-],[])[.orig.tar.gz
$1-deb: \$($1-deb_SOURCE)
$1-osx-pkg = osx/packages/$1-]pkgver[.pkg
\$($1-osx-pkg): $1-stamp
\$($1-osx-pkg)_SUBDIR = $2
\$($1-osx-pkg)_PACKAGE_NAME = $1
\$($1-osx-pkg)_PACKAGE_VERSION = ]pkgver[
$1-osx-pkg: \$($1-osx-pkg)
])
    m4_append([GT_PKGCONFIG_DIRS_MACRO], ${ac_pwd}/[$2], [:])
    ifelse([$1], [gsi-openssh], [], [
        ifelse([$1], [gridftp_hdfs], [], [
            ifelse([$1], [myproxy], [], [
                    m4_append([GT_SUBDIRS_MACRO], [$2], [ ])])])])dnl
    ifelse([$1], [gsi_openssh], [], [
        m4_append([GT_TARGETS_CHECK_MACRO], [$1]-check, [ ])])dnl
    m4_append_uniq([GT_BUNDLES_MACRO],[$4], [ ])dnl
    m4_append([GT_BUNDLES_]m4_translit($4,[-],[_])[_MACRO],[$1-stamp], [ ])dnl
    m4_append([GT_BUNDLE_PACKAGES]m4_translit($4,[-],[_]),[$1], [ ])dnl
    m4_append([GT_DOXYGEN_SUBDIRS_MACRO], [$srcdir/$2], [ ])dnl
    m4_append([GT_ONLY_MACRO], [$1-only], [ ])dnl
    m4_append([GT_BUNDLES_]$4[_PACKAGES],native_name)
    m4_append([GT_PACKAGE_LIST], [$1], [ ])dnl
    m4_append([GT_ALL_STAMPS_MACRO], [$1]-stamp, [ ])dnl
    m4_define(underscored_name[_VERSION], pkgver)
    m4_append(underscored_name[_BUNDLE], $4)
    m4_append([GT_BUNDLES_]m4_translit([$4], [-], [_])[_XIO_DRIVERS],
        [ifelse([$5],[1], [$1-stamp ],[])])
    m4_append([GT_TARGETS_CLEAN_MACRO], [$1-clean], [ ])dnl
    m4_foreach([req], m4_split(m4_normalize($3)), [dnl
        m4_append(m4_translit(req,[-],[_])[_REQUIRES], [choices]@<:@'org.globus.package.$1'@:>@[.selected], [ || ])])dnl
])dnl

GT_PACKAGE(
    [globus_common], [common/source],[libltdl],[ccommonlibs])
GT_PACKAGE(
    [globus_xio], [xio/src], [globus_common],[ccommonlibs])
GT_PACKAGE(
    [globus_gfork], [gridftp/gfork/source], [globus_common globus_xio], [gridftp])
GT_PACKAGE(
    [globus_gsi_openssl_error], [gsi/openssl_error/source], [globus_common],
    [gsi])
GT_PACKAGE(
    [globus_gsi_proxy_ssl], [gsi/proxy/proxy_ssl/source], [], [gsi])
GT_PACKAGE(
    [globus_openssl_module], [gsi/openssl_module/source],
    [globus_common globus_gsi_proxy_ssl globus_gsi_openssl_error],
    [gsi])
GT_PACKAGE(
    [globus_gsi_cert_utils], [gsi/cert_utils/source],
    [globus_common globus_openssl_module globus_gsi_openssl_error 
     globus_gsi_proxy_ssl], [gsi])
GT_PACKAGE([globus_gsi_sysconfig], [gsi/sysconfig/source], 
    [globus_common globus_openssl_module globus_gsi_openssl_error], [gsi])
GT_PACKAGE([globus_callout], [callout/source], [globus_common], [ccommonlibs])
GT_PACKAGE([globus_gsi_callback], [gsi/callback/source],
    [ globus_common globus_openssl_module globus_gsi_openssl_error 
      globus_gsi_cert_utils globus_gsi_sysconfig globus_gsi_proxy_ssl ],
    [gsi])
GT_PACKAGE([globus_gsi_credential], [gsi/credential/source],
    [ globus_common globus_gsi_proxy_ssl globus_gsi_openssl_error 
      globus_gsi_cert_utils globus_gsi_sysconfig globus_gsi_callback],
    [gsi])
GT_PACKAGE([globus_gsi_proxy_core], [gsi/proxy/proxy_core/source],
    [ globus_common globus_openssl_module globus_gsi_openssl_error 
      globus_gsi_cert_utils globus_gsi_sysconfig globus_gsi_proxy_ssl 
      globus_gsi_credential ], [gsi])
GT_PACKAGE([globus_gssapi_gsi], [gsi/gssapi/source], 
    [ globus_common globus_openssl_module globus_gsi_openssl_error 
      globus_gsi_cert_utils globus_gsi_credential globus_gsi_callback 
      globus_gsi_proxy_core globus_gsi_sysconfig ], [gsi])
GT_PACKAGE([globus_gss_assist],[gsi/gss_assist/source],
    [ globus_common globus_gsi_sysconfig globus_gsi_cert_utils 
      globus_gssapi_gsi globus_callout globus_gsi_credential],
      [gsi])
GT_PACKAGE([globus_gssapi_error], [gsi/gssapi_error/source],
    [ globus_common  globus_gss_assist  globus_gssapi_gsi ], [gsi])
GT_PACKAGE([globus_xio_gsi_driver],[xio/drivers/gsi],
    [ globus_xio  globus_gss_assist  globus_gssapi_error  
      globus_gssapi_gsi  globus_common ], [ccommonlibs], 1)
GT_PACKAGE([globus_io],[io/compat], 
    [ globus_common  globus_xio  globus_gss_assist  globus_gssapi_gsi  
      globus_xio_gsi_driver  globus_gssapi_error ], [ccommonlibs])
GT_PACKAGE([globus_ftp_control],[gridftp/control/source],
    [ globus_common  globus_gss_assist  globus_gssapi_gsi  globus_io
      globus_xio  globus_gssapi_error ], [gridftp])
GT_PACKAGE([globus_usage],[usage/c/sender/source],
    [globus_common globus_xio], [ccommonlibs])
GT_PACKAGE([globus_xio_pipe_driver],[xio/drivers/pipe/source],
    [globus_xio globus_common], [ccommonlibs], 1)
GT_PACKAGE([globus_gridftp_server_control],[gridftp/server-lib/src],
    [ globus_common  globus_xio  globus_xio_gsi_driver  globus_xio_pipe_driver
      globus_gss_assist  globus_gssapi_gsi  globus_gsi_openssl_error
      globus_gssapi_error ], [gridftp])
GT_PACKAGE([globus_authz_callout_error],[gsi/authz/error],
    [globus_common], [gsi])
GT_PACKAGE([globus_authz],[gsi/authz/source],
    [globus_common globus_callout globus_gssapi_gsi
    globus_authz_callout_error], [gsi])
GT_PACKAGE([globus_xio_rate_driver],[xio/drivers/rate/source],
    [globus_xio], [ccommonlibs], 1)
GT_PACKAGE([globus_xio_popen_driver],[xio/drivers/popen/source],
    [globus_common globus_xio], [ccommonlibs], 1)
GT_PACKAGE([globus_xio_udt_driver],[xio/drivers/udt/source], [globus_common globus_xio], [udt], 1)
GT_PACKAGE([globus_net_manager],[gridftp/net_manager],
    [globus_common globus_xio], [gridftp])
GT_PACKAGE([globus_gridftp_server],[gridftp/server/src],
    [ globus_common  globus_xio  globus_xio_gsi_driver  globus_gfork
      globus_gridftp_server_control  globus_ftp_control  globus_authz
      globus_usage  globus_gss_assist  globus_gsi_credential ],
    [gridftp])
GT_PACKAGE([globus_gram_protocol],[gram/protocol/source],
    [ globus_common globus_io globus_gssapi_gsi globus_gss_assist], [gram5])
GT_PACKAGE([globus_scheduler_event_generator],
    [gram/jobmanager/scheduler_event_generator/source],
    [ globus_common  globus_xio  globus_gram_protocol  globus_xio_gsi_driver],
    [gram5-server])
GT_PACKAGE([globus_gram_job_manager_lsf],
    [gram/jobmanager/lrms/lsf/source],
    [ globus_common  globus_xio  globus_scheduler_event_generator
      globus_gram_protocol], [gram5-lsf])
GT_PACKAGE([globus_gass_transfer],[gass/transfer/source],
    [globus_common globus_gssapi_gsi globus_gss_assist globus_io],
    [ccommonlibs])
GT_PACKAGE([globus_ftp_client],[gridftp/client/source],
    [ globus_common  globus_ftp_control  globus_gsi_callback
      globus_gsi_credential  globus_gsi_sysconfig  globus_gssapi_gsi
      globus_xio globus_xio_popen_driver ], [gridftp])
GT_PACKAGE([globus_gass_copy],[gass/copy/source],
    [ globus_common  globus_ftp_client  globus_ftp_control
      globus_gsi_sysconfig  globus_gass_transfer  globus_io  globus_gssapi_gsi
      globus_gssapi_error ], [gridftp])
GT_PACKAGE([globus_gass_cache],[gass/cache/source],
    [ globus_common ], [gram5])
GT_PACKAGE([globus_rsl],[gram/rsl/source], [globus_common], [gram5])
GT_PACKAGE([globus_gram_job_manager_callout_error],
        [gram/jobmanager/callout_error],
        [globus_common], [gram5-server])
GT_PACKAGE([globus_xio_gridftp_multicast],
        [gridftp/server/multi/source],
        [ globus_xio  globus_ftp_client  globus_xio_gsi_driver ],
        [gridftp])
GT_PACKAGE([globus_simple_ca],[gsi/simple_ca/source], [], [gsi])
GT_PACKAGE([globus_gram_job_manager_sge],
        [gram/jobmanager/lrms/sge/source],
        [ globus_common  globus_xio  globus_scheduler_event_generator
          globus_gram_protocol ], [gram5-sge])
GT_PACKAGE([globus_gram_job_manager_scripts],
        [gram/jobmanager/scripts], [], [gram5-server])
GT_PACKAGE([globus_gram_job_manager_slurm],
        [gram/jobmanager/lrms/slurm/source], [], [gram5-slurm])
GT_PACKAGE([globus_gridmap_callout_error],
        [gsi/gridmap_callout/error], [globus_common globus_gssapi_error],
        [gsi])
GT_PACKAGE([globus_gridmap_callout], [gsi/gridmap_callout/source],
    [ globus_common globus_gssapi_gsi globus_gss_assist
      globus_gridmap_callout_error globus_gsi_credential ], [gsi])
GT_PACKAGE([globus_gass_server_ez],[gass/server_ez/source],
    [ globus_common  globus_gass_transfer  globus_gssapi_gsi 
      globus_gss_assist ], [gram5])
GT_PACKAGE([globus_xio_gridftp_driver],
    [gridftp/gridftp_driver/source],
    [ globus_common  globus_xio  globus_ftp_client  globus_xio_gsi_driver ],
    [gridftp], 1)
GT_PACKAGE([globus_gram_job_manager_fork],
    [gram/jobmanager/lrms/fork/source],
    [ globus_common  globus_xio  globus_scheduler_event_generator
      globus_gram_protocol ], [gram5-server])
GT_PACKAGE([globus_gridmap_verify_myproxy_callout],
    [gsi/gridmap_verify_myproxy_callout],
    [ globus_gsi_sysconfig  globus_gssapi_gsi  globus_gss_assist
      globus_gridmap_callout_error  globus_gsi_credential ],
      [gsi])
GT_PACKAGE([globus_gridmap_eppn_callout], [gsi/gridmap_eppn_callout],
    [ globus_gsi_sysconfig globus_gssapi_gsi globus_gss_assist
      globus_gridmap_callout_error globus_gsi_credential ],
    [gsi])
GT_PACKAGE([globus_gram_job_manager_condor],
    [gram/jobmanager/lrms/condor/source], [], [gram5-condor])
GT_PACKAGE([globus_proxy_utils],[gsi/proxy/proxy_utils/source],
    [ globus_common globus_openssl_module globus_gsi_openssl_error
      globus_gsi_cert_utils globus_gsi_sysconfig globus_gsi_proxy_ssl
      globus_gsi_credential globus_gsi_callback globus_gsi_proxy_core
      globus_gss_assist ], [gsi])
GT_PACKAGE([globus_gram_client],[gram/client/source], 
    [ globus_common  globus_gram_protocol  globus_io  globus_rsl
      globus_gss_assist], [gram5])
GT_PACKAGE([globus_xioperf],[xio/xioperf/source],
    [ globus_common globus_xio ], [ccommonlibs])
GT_PACKAGE([globus_gatekeeper],[gram/gatekeeper/source],
    [ globus_common  globus_gss_assist  globus_gssapi_gsi ], [gram5-server])
GT_PACKAGE([globus_gass_cache_program],[gass/cache_program/source],
    [ globus_common  globus_gass_cache  globus_gass_copy  globus_gass_transfer
      globus_gass_server_ez  globus_gram_client ], [gram5])
GT_PACKAGE([globus_gram_job_manager_pbs],
    [gram/jobmanager/lrms/pbs/source],
    [globus_common globus_scheduler_event_generator], [gram5-pbs])
GT_PACKAGE([globus_gram_audit],
    [gram/jobmanager/auditing/source],
    [], [gram5-auditing])
GT_PACKAGE([myproxy],[myproxy/source],
    [ globus_common globus_gss_assist globus_gssapi_gsi globus_proxy_utils
      globus_usage ], [myproxy])
GT_PACKAGE([gsi-openssh], [gsi_openssh/source], 
    [globus_gss_assist globus_usage], [gsi-openssh])
GT_PACKAGE([globus_gram_client_tools],[gram/client_tools/source],
    [ globus_common  globus_gram_client  globus_gram_protocol
      globus_gass_transfer  globus_gass_server_ez  globus_rsl
      globus_gss_assist ], [gram5])
GT_PACKAGE([globus_gram_job_manager],[gram/jobmanager/source],
    [ globus_common  globus_gsi_credential  globus_gass_cache
      globus_gass_transfer  globus_gram_protocol  globus_gssapi_gsi
      globus_gss_assist  globus_gsi_sysconfig  globus_callout  globus_xio
      globus_xio_popen_driver  globus_rsl
      globus_gram_job_manager_callout_error globus_scheduler_event_generator
      globus_usage globus_gram_client_tools], [gram5-server])
GT_PACKAGE([gridftp_hdfs],[gridftp/hdfs],
    [ globus_gridftp_server ], [gridftp-hdfs])

dnl Augment PKG_CONFIG_PATH with the paths to all of the build directories so
dnl that flags can be constructed automatically
PKG_CONFIG_PATH="[${PKG_CONFIG_PATH:+$PKG_CONFIG_PATH:}]GT_PKGCONFIG_DIRS_MACRO"
export PKG_CONFIG_PATH
if test "x$ac_cv_env_PKG_CONFIG_set" != "xset"; then
    AC_PATH_TOOL([PKG_CONFIG], [pkg-config])
fi
dnl Similar to PKG_PROG_PKG_CONFIG, but without setting AC_ARG_VAR for
dnl PKG_CONFIG at the top level so we can override it.
pkg_config_min=0.22
if test -n "$PKG_CONFIG"; then
    AC_MSG_CHECKING([pkg-config is at least version $pkg_config_min])
    if $PKG_CONFIG --atleast-pkgconfig-version $pkg_config_min; then
            AC_MSG_RESULT([yes])
    else
        AC_MSG_RESULT([no])
        PKG_CONFIG=""
    fi
fi

if test x"$PKG_CONFIG" = x ; then
   AC_MSG_ERROR([Missing required pkg-config >= $pkg_config_min])
fi

dnl This is used by the OSX package generation
OPENSSL_VERSION=$($PKG_CONFIG --modversion openssl)
AC_SUBST([OPENSSL_VERSION])

gsi_openssh_can_recurse=yes

dnl 
if test "$HAVE_LTDL" = 1; then
    TOP_SUBDIRS="."
else
    TOP_SUBDIRS="libltdl ."
fi
TOP_DIST_SUBDIRS="libltdl GT_SUBDIRS_MACRO"
GT_TARGETS_CLEAN="GT_TARGETS_CLEAN_MACRO"
GT_TARGETS_DIST="GT_TARGETS_DIST_MACRO"
GT_PKGCONFIG_DIRS="GT_PKGCONFIG_DIRS_MACRO"
GT_DOXYGEN_SUBDIRS="GT_DOXYGEN_SUBDIRS_MACRO"
dnl These could live in Makefile.am if automake could handle these variable
dnl names
m4_append([TARGETS_MACRO], [
ltdl-$LTDL_VERSION.pkg_SUBDIR = libltdl
ltdl-$LTDL_VERSION.pkg_PACKAGE_NAME = libltdl
ltdl-$LTDL_VERSION.pkg_PACKAGE_VERSION = $LTDL_VERSION
])

m4_foreach([bundle], m4_split(GT_BUNDLES_MACRO),
    [ifelse(
        m4_expand(m4_substr(bundle, 0, 6)), [gram5-],dnl
        [m4_append([GT_DISABLED_BUNDLES_MACRO], bundle, [ ])],
        [ifelse(
            m4_expand(m4_substr(bundle, 0, 3)), [udt],
            [m4_append([GT_DISABLED_BUNDLES_MACRO], bundle, [ ])],dnl
            [ifelse(
                m4_expand(m4_substr(bundle, 0, 12)), [gridftp-hdfs],
                [m4_append([GT_DISABLED_BUNDLES_MACRO], bundle, [ ])],dnl
                [ifelse(
                    m4_expand(m4_substr(bundle, 0, 7)), [myproxy],
                    [m4_append([GT_DISABLED_BUNDLES_MACRO], bundle, [ ])],dnl
                    [m4_append([GT_ENABLED_BUNDLES_MACRO], bundle, [ ])])])])])])

enabled_buns="GT_ENABLED_BUNDLES_MACRO"
m4_foreach([bundle],
    m4_split(GT_DISABLED_BUNDLES_MACRO),
    [
    AC_ARG_ENABLE(bundle, AS_HELP_STRING([--enable-]bundle, [Enable ]bundle),
        [if test X"$enableval" = X"yes"; then
            skip=0
            for name in $enabled_buns; do
                if test "$name" = bundle ; then
                    skip=1
                    break
                fi
            done
            if test "$skip" = 0; then
                enabled_buns="${enabled_buns:+$enabled_buns }bundle"
            fi
        elif test X"$enableval" = X"no"; then
            skip=0
            for name in $disabled_buns; do
                if test "$name" = bundle ; then
                    skip=1
                    break
                fi
            done
            if test "$skip" = 0; then
                disabled_buns="${disabled_buns:+$disabled_buns }bundle"
            fi
        fi])
])
m4_foreach([bundle],
    m4_split(GT_ENABLED_BUNDLES_MACRO),
    [
    AC_ARG_ENABLE(bundle, AS_HELP_STRING([--disable-]bundle, [Disable ]bundle),
        [if test X"$enableval" = X"yes"; then
            skip=0
            for name in $enabled_buns; do
                if test "$name" = bundle ; then
                    skip=1
                    break
                fi
            done
            if test "$skip" = 0; then
                enabled_buns="${enabled_buns:+$enabled_buns }bundle"
            fi
        elif test X"$enableval" = X"no"; then
            skip=0
            for name in $disabled_buns; do
                if test "$name" = bundle ; then
                    skip=1
                    break
                fi
            done
            if test "$skip" = 0; then
                disabled_buns="${disabled_buns:+$disabled_buns }bundle"
            fi
        fi])
])

AC_ARG_ENABLE(all, AS_HELP_STRING([--enable-all], [Build all optional parts of the toolkit]),
    [if test X"$enableval" = X"yes"; then
         enable_all=yes
     else
         enable_all=no
     fi],
    [enable_all=no])

if test "$enable_all" = yes; then
    disabled_buns=""
    enabled_buns="GT_BUNDLES_MACRO"
fi

final_enabled_bundles=""
final_enabled_xio_drivers=""
gsi_openssh_enabled=no
myproxy_enabled=no
gridftp_hdfs_enabled=no
for en_name in ""$enabled_buns; do
    skip=0
    for dis_name in ""$disabled_buns; do
        if test "$en_name" = "$dis_name"; then
            skip=1
            break
        fi
    done
    if test "$skip" = 0; then
        if test X"$en_name" = X"gsi-openssh"; then
            gsi_openssh_enabled=yes
        elif test X"$en_name" = X"myproxy"; then
            myproxy_enabled=yes
        elif test X"$en_name" = X"gridftp-hdfs"; then
            gridftp_hdfs_enabled=yes
        fi
        final_enabled_bundles="${final_enabled_bundles:+$final_enabled_bundles }$en_name"
        final_enabled_xio_drivers="${final_enabled_xio_drivers:+$final_enabled_xio_drivers }\$(${en_name}-xio-drivers)"
    fi
done
TARGETS="TARGETS_MACRO m4_foreach([bundle],
    m4_split(GT_BUNDLES_MACRO),
    [
bundle[-deps] =  m4_expand([GT_BUNDLES_]m4_translit(bundle,[-],[_])[_MACRO])
bundle: \$(bundle[-deps])
bundle[-xio-drivers] = m4_expand([GT_BUNDLES_]m4_translit(bundle,[-],[_])[_XIO_DRIVERS])
])"
GT_BUNDLES="m4_normalize(GT_BUNDLES_MACRO)"
AC_PATH_PROGS([DOXYGEN], [doxygen])
AM_CONDITIONAL([ENABLE_DOXYGEN], [test "$DOXYGEN" != ""])
TARGETS_FILE=makefile.targets
echo "$TARGETS" > "$TARGETS_FILE"

GT_PACKAGE_VERSIONS_SH="osx/GT_PKG_LIST_SH"
MAC_INSTALLER_PKG_REFS="osx/MAC_INSTALLER_PKG_REFS"
MAC_INSTALLER_CHOICE_LINES="osx/MAC_INSTALLER_CHOICE_LINES"
MAC_INSTALLER_PACKAGE_CHOICES="osx/MAC_INSTALLER_PACKAGE_CHOICES"
rm -f "${GT_PACKAGE_VERSIONS_SH}" "$MAC_INSTALLER_PKG_REFS" "$MAC_INSTALLER_CHOICE_LINES" "$MAC_INSTALLER_PACKAGE_CHOICES"
mkdir -p osx

touch $GT_PACKAGE_VERSIONS_SH $MAC_INSTALLER_PKG_REFS $MAC_INSTALLER_CHOICE_LINES $MAC_INSTALLER_PACKAGE_CHOICES
GT_TARGETS_OSX_PKG=""
GT_TARGETS_RPM=""
GT_TARGETS_DEB=""
GT_TARGETS_ALL=""
GT_TARGETS_INSTALL=""
GT_TARGETS_DIST=""
GT_ALL_TARGETS_OSX_PKG=""
GT_ALL_TARGETS_DEB=""
GT_ALL_TARGETS_RPM=""
GT_ALL_TARGETS_ALL=""
GT_ALL_TARGETS_DIST=""
GT_ALL_TARGETS_INSTALL=""

m4_foreach([packname], m4_split(GT_PACKAGE_LIST), [
    m4_define([underscored_name], m4_translit(packname,[-],[_]))
    for bun in $final_enabled_bundles; do
        if test "m4_expand(underscored_name[_BUNDLE])" = "$bun"; then
            cat <<EOF >> "$MAC_INSTALLER_PKG_REFS"
    <pkg-ref id="org.globus.packname" version="m4_expand(underscored_name[_VERSION])">packname[-]m4_expand(underscored_name[_VERSION]).pkg</pkg-ref>
EOF
            cat <<EOF >> "$MAC_INSTALLER_PACKAGE_CHOICES"
    <choice 
        id="org.globus.package.packname"
        visible="false"
        title="packname"
        description="packname Libraries and Tools"dnl
m4_ifdef(underscored_name[_REQUIRES],[
        selected="m4_expand(underscored_name[_REQUIRES])"])
        start_selected="choices@<:@'org.globus.bundle.m4_expand(underscored_name[_BUNDLE])'@:>@.selected">
        <pkg-ref id="org.globus.packname"/>
    </choice>
EOF
        GT_TARGETS_OSX_PKG="${GT_TARGETS_OSX_PKG:+$GT_TARGETS_OSX_PKG }\$(packname[-osx-pkg])"
        GT_TARGETS_DEB="${GT_TARGETS_DEB:+$GT_TARGETS_DEB }packname[-deb]"
        GT_TARGETS_RPM="${GT_TARGETS_RPM:+$GT_TARGETS_RPM }packname[-rpm]"
        GT_TARGETS_ALL="${GT_TARGETS_ALL:+$GT_TARGETS_ALL }packname[-all]"
        GT_TARGETS_DIST="${GT_TARGETS_DIST:+$GT_TARGETS_DIST }packname[-dist]"
        GT_TARGETS_INSTALL="${GT_TARGETS_INSTALL:+$GT_TARGETS_INSTALL }packname[-install]"
        GT_PACKAGE_LIST_SH="${GT_PACKAGE_LIST_SH:+$GT_PACKAGE_LIST_SH }packname"
        echo "underscored_name[_version]=m4_expand(underscored_name[_VERSION])" >> "$GT_PACKAGE_VERSIONS_SH"
        fi
    done
    GT_ALL_TARGETS_OSX_PKG="${GT_ALL_TARGETS_OSX_PKG:+$GT_ALL_TARGETS_OSX_PKG }packname[-osx-pkg]"
    GT_ALL_TARGETS_DEB="${GT_ALL_TARGETS_DEB:+$GT_ALL_TARGETS_DEB }packname[-deb]"
    GT_ALL_TARGETS_RPM="${GT_ALL_TARGETS_RPM:+$GT_ALL_TARGETS_RPM }packname[-rpm]"
    GT_ALL_TARGETS_ALL="${GT_ALL_TARGETS_ALL:+$GT_ALL_TARGETS_ALL }packname[-all]"
    GT_ALL_TARGETS_DIST="${GT_ALL_TARGETS_DIST:+$GT_ALL_TARGETS_DIST }packname[-dist]"
    GT_ALL_TARGETS_INSTALL="${GT_ALL_TARGETS_INSTALL:+$GT_ALL_TARGETS_INSTALL }packname[-install]"
])

m4_foreach([bundlename],
    m4_split(GT_BUNDLES_MACRO),
    [
    for bun in $final_enabled_bundles; do
        if test bundlename = "$bun"; then
            cat <<EOF >> "$MAC_INSTALLER_CHOICE_LINES"
    <line choice="org.globus.bundle.bundlename">
    m4_foreach([pkgname], m4_split(m4_expand([GT_BUNDLE_PACKAGES]m4_translit(bundlename,[-],[_]))), [
        <line choice="org.globus.package.pkgname"/>])
    </line>
EOF
        fi
    done
    ])
    
if test "${gsi_openssh_can_recurse}" != yes && \
   test x"$gsi_openssh_enabled" = x"yes"; then
    AC_MSG_ERROR([Unable to build with gsi-openssh])
fi
AC_SUBST_FILE(MAC_INSTALLER_PKG_REFS)
AC_SUBST_FILE(MAC_INSTALLER_CHOICE_LINES)
AC_SUBST_FILE(MAC_INSTALLER_PACKAGE_CHOICES)

AC_SUBST_FILE(TARGETS_FILE)
AC_SUBST(TOP_SUBDIRS)
AC_SUBST(TOP_DIST_SUBDIRS)
AC_SUBST(GT_TARGETS_ALL)
AC_SUBST(GT_TARGETS_CLEAN)
AC_SUBST(GT_TARGETS_INSTALL)
GT_TARGETS_CHECK="GT_TARGETS_CHECK_MACRO"
AC_SUBST(GT_TARGETS_CHECK)
AC_SUBST(GT_TARGETS_DIST)
AC_SUBST(GT_TARGETS_RPM)
AC_SUBST(GT_TARGETS_DEB)
AC_SUBST(GT_TARGETS_OSX_PKG)
AC_SUBST(GT_ALL_TARGETS_OSX_PKG)
AC_SUBST(GT_ALL_TARGETS_DEB)
AC_SUBST(GT_ALL_TARGETS_RPM)
AC_SUBST(GT_ALL_TARGETS_ALL)
AC_SUBST(GT_ALL_TARGETS_DIST)
AC_SUBST(GT_ALL_TARGETS_INSTALL)
AC_SUBST(GT_PKGCONFIG_DIRS)
AC_SUBST(GT_DOXYGEN_SUBDIRS)
GT_ALL_STAMPS="GT_ALL_STAMPS_MACRO"
AC_SUBST(GT_ALL_STAMPS)
AC_SUBST(GT_ENABLED_BUNDLES, ["$final_enabled_bundles"])
AC_SUBST(GT_ENABLED_XIO_DRIVERS, ["$final_enabled_xio_drivers"])
AC_SUBST(GT_BUNDLES)
AC_SUBST(GT_PACKAGE_LIST_SH)
AC_SUBST_FILE(GT_PACKAGE_VERSIONS_SH)
AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([Doxyfile])
AC_CONFIG_FILES([doxygen/index.dox])
AC_CONFIG_FILES([osx/create-packages], [chmod a+x osx/create-packages])
AC_CONFIG_FILES([osx/distribution.xml])
AC_CONFIG_FILES([osx/scripts/globus_common/postinstall], [chmod a+x osx/scripts/globus_common/postinstall])

AC_CONFIG_SUBDIRS(GT_SUBDIRS_MACRO)
if test "${gsi_openssh_can_recurse}" = yes; then
    if test x"$gsi_openssh_enabled" = x"yes"; then
        AC_CONFIG_SUBDIRS(gsi_openssh/source)
    fi
fi

if test x"$gridftp_hdfs_enabled" = x"yes"; then
    AC_CONFIG_SUBDIRS(gridftp/hdfs)
fi
if test x"$myproxy_enabled" = x"yes"; then
    AC_CONFIG_SUBDIRS(myproxy/source)
fi

AC_OUTPUT
