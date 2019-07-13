dnl Process this file with autoconf to produce a configure script.
AC_INIT([myproxy],[6.2.6])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign])
LT_INIT([dlopen win32-dll])

AC_CONFIG_MACRO_DIR([m4])
PACKAGE_VERSION2=${PACKAGE_VERSION%.*}
AC_SUBST([MAJOR_VERSION], [${PACKAGE_VERSION2%%.*}])
AC_SUBST([MINOR_VERSION], [${PACKAGE_VERSION2##*.}])
AC_SUBST([MICRO_VERSION], [${PACKAGE_VERSION##*.}])
AC_SUBST([AGE_VERSION], [0])
AC_SUBST([PACKAGE_DEPS], ["globus-gssapi-gsi >= 9, globus-gss-assist >= 8, globus-gsi-sysconfig >= 5, globus-gsi-cert-utils >= 8, globus-gsi-proxy-core >= 6, globus-gsi-credential >= 5, globus-gsi-callback >= 4, globus-common >= 14"])

m4_define([myproxy_date], m4_syscmd([
if git rev-parse --show-toplevel > /dev/null 2>&1; then
    git log -n 1 --pretty=format:%cD | awk '{printf $3 " " $4}' > myproxy-date.inc.new;
    if ! cmp myproxy-date.inc myproxy-date.inc.new > /dev/null 2>&1; then
        mv myproxy-date.inc.new myproxy-date.inc
    else
        rm myproxy-date.inc.new
    fi
fi])dnl
m4_incl[ude(myproxy-date.inc)])dnl
MYPROXY_DATE="myproxy_date"
AC_SUBST([MYPROXY_DATE])

PKG_CHECK_EXISTS([globus-proxy-utils], [
    GSI_PROXY_UTILS_PATH="$($PKG_CONFIG --variable=path globus-proxy-utils)"])
AC_SUBST([GSI_PROXY_UTILS_PATH])
PKG_CHECK_EXISTS([globus-gsi-cert-utils], [
    GSI_CERT_UTILS_PATH="$($PKG_CONFIG --variable=path globus-gsi-cert-utils)"])
AC_SUBST([GSI_CERT_UTILS_PATH])

AC_PROG_LN_S

dnl
dnl Figure out which regular expression parser to use.
dnl  (taken from krb5/lib/krb5/os/configure.in)
dnl Try, in order, regcomp, compile/step
dnl
AC_HAVE_HEADERS(regex.h regexpr.h)
dnl
dnl regcomp (which is present but non-functional on Solaris 2.4)
dnl
AC_CACHE_CHECK([for working regcomp],
  [ac_cv_func_regcomp],
  [AC_TRY_RUN([
#include <sys/types.h>
#include <regex.h>
regex_t x; regmatch_t m;
int main() { return regcomp(&x,"pat.*",0) || regexec(&x,"pattern",1,&m,0); }
    ], ac_cv_func_regcomp=yes, ac_cv_func_regcomp=no, ac_cv_func_regcomp=no)])
dnl
dnl compre/step
dnl
save_LIBS=$LIBS
LIBS=-lgen
dnl this will fail if there's no compile/step in -lgen, or if there's
dnl no -lgen.  This is fine.
AC_CHECK_FUNCS(compile step, have_func_compile=yes)
LIBS=$save_LIBS
dnl
if test "$ac_cv_func_regcomp" = yes ; then
  AC_MSG_RESULT([Using re_comp for regular expression matching])
  AC_DEFINE(HAVE_REGCOMP)
elif test "$have_func_compile" = yes ; then
  AC_MSG_RESULT([Using compile/step for regular expression matching])
  AC_CHECK_LIB(gen, compile, [SERVERLIBS="$SERVERLIBS -lgen"])
else
  AC_MSG_ERROR([No support for regular expression matching])
fi
dnl
dnl Check for setenv()
dnl
AC_CHECK_FUNCS(setenv)
dnl
dnl Check for unsetenv()
dnl
AC_CHECK_FUNCS(unsetenv)
dnl
dnl Check for getopt_long()
dnl
AC_HAVE_HEADERS(getopt.h)
AC_CHECK_FUNCS(getopt_long)
dnl
dnl Check for socklen_t
dnl
AC_CHECK_HEADERS([sys/socket.h])

AC_CHECK_TYPES([socklen_t],[],[],AC_INCLUDES_DEFAULT[
#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif])
dnl
dnl Check for facilitynames
dnl
AC_CHECK_DECLS([facilitynames], [], [],
  [[
        #define SYSLOG_NAMES
        #include <stdio.h>
        #include <syslog.h>
  ]])
dnl
dnl Check to see if we need "netlibs" (specifically, libnsl and libsocket)
dnl
uname=`(uname) 2>/dev/null`
if test "$uname" != IRIX -a "$uname" != IRIX64 ; then
	AC_CHECK_LIB(socket, socket,
[	SYSLIBS="$SYSLIBS -lsocket"], ,-lnsl)
	AC_CHECK_LIB(nsl, t_bind,
[	SYSLIBS="$SYSLIBS -lnsl"])
fi
AC_SUBST(SYSLIBS)

if test "x$PKG_CONFIG_PATH" = "x" -a "x$GLOBUS_LOCATION" != "x" ; then
   PKG_CONFIG_PATH=$GLOBUS_LOCATION/lib/pkgconfig
   export PKG_CONFIG_PATH
fi
PKG_CHECK_MODULES([GLOBUS], [$PACKAGE_DEPS], [], AC_MSG_ERROR([GLOBUS_PKG_ERRORS]))

PKG_CHECK_MODULES([OPENSSL], [openssl >= 1.0],
    [OPENSSL_PKGCONFIG="openssl >= 1.0"])
CPPFLAGS="$OPENSSL_CFLAGS $CPPFLAGS"
LIBS="$OPENSSL_LIBS $LIBS"

AC_PATH_PROGS([OPENSSL], openssl)
AM_CONDITIONAL([ENABLE_TESTS], [test "x$OPENSSL" != x])

dnl
dnl Check for globus_gsi_proxy_handle_set_extensions
dnl
AC_CHECK_LIB(globus_gsi_proxy_core,
             globus_gsi_proxy_handle_set_extensions,
             AC_DEFINE(HAVE_GLOBUS_GSI_PROXY_HANDLE_SET_EXTENSIONS),
             )
dnl
dnl Check for OCSP
dnl
AC_CHECK_FUNC(OCSP_basic_verify, AC_DEFINE(HAVE_OCSP))
AC_ARG_WITH(sasl2,
    AS_HELP_STRING([--with-sasl2=PATH], [Build with SASL V2 support]),
	[
		if test "x$withval" = "xyes" ; then
		   AC_MSG_ERROR([--with-sasl2 requires PATH argument])
		fi
		if test "x$withval" != "xno" ; then
		   CPPFLAGS="-I${withval}/include/sasl $CPPFLAGS"
		   LDFLAGS="-L${withval}/lib $LDFLAGS"
		   AC_CHECK_HEADER(sasl.h)
		   AC_CHECK_LIB(sasl2, sasl_client_init, ,
				AC_MSG_ERROR([libsasl2 not found]))
		   AC_MSG_CHECKING(that sasl.h matches libsasl2)
		   AC_TRY_RUN([
#include <sasl.h>
int main() { if (SASL_VERSION_MAJOR != 2) exit(1); else exit(0); }
		      ],
		      [AC_MSG_RESULT([yes])],
		      [
			AC_MSG_RESULT([no])
			AC_MSG_ERROR([sasl.h does not match libsasl2])
		      ],
		      [AC_MSG_RESULT([yes])])
		   SASL="yes"
		fi
	]
)
dnl
dnl Check for Kerberos
dnl
AC_ARG_WITH(kerberos5,
    AS_HELP_STRING([--with-kerberos5=PATH],[Build with Kerberos V5 support]),
       [
               if test "x$withval" = "xyes" ; then
                  AC_MSG_ERROR([--with-kerberos5 requires PATH argument])
               fi
               if test "x$withval" != "xno" ; then
                  if test "$SASL" != "yes"; then
                     AC_MSG_ERROR([--with-kerberos5 requires --with-sasl2])
                  fi
                  KRB5CPPFLAGS="-I${withval}/include $CPPFLAGS"
                  AC_SUBST(KRB5CPPFLAGS)
                  AC_DEFINE(BUILD_GSSAPI_PLUGIN)
                  AC_CHECK_HEADERS(gssapi.h)
               fi
       ]
)
dnl
dnl Check for PAM
dnl
AC_CHECK_HEADERS(security/pam_appl.h pam/pam_appl.h)
if test "x$ac_cv_header_security_pam_appl_h" = "xyes" || \
   test "x$ac_cv_header_pam_pam_appl_h" = "xyes" ; then
   AC_CHECK_LIB(dl, dlopen, , )
   AC_CHECK_LIB(pam, pam_set_item, , )
fi

dnl
dnl Check for pthread_sigmask
dnl
AC_CHECK_FUNC(pthread_sigmask, AC_DEFINE(HAVE_PTHREAD_SIGMASK))

dnl
dnl Check for pidfile_open
dnl
AC_CHECK_DECL(pidfile_open, AC_DEFINE(HAVE_PIDFILE_DECL),  ,
              [#include <libutil.h>])
AC_SEARCH_LIBS(pidfile_open, util bsd, AC_DEFINE(HAVE_PIDFILE))

dnl
dnl Check for OpenLDAP
dnl

AC_ARG_WITH(openldap,
    AS_HELP_STRING([--with-openldap=PATH],[Build with OpenLDAP CA support]),
       [
               if test "x$withval" = "xyes" ; then
                  AC_MSG_ERROR([--with-openldap requires PATH argument])
               fi
               if test "x$withval" != "xno" ; then
		  CPPFLAGS="-I${withval}/include $CPPFLAGS"
		  AC_MSG_CHECKING(for OpenLDAP v2.3 or later)
          AC_TRY_RUN([
#include <ldap.h>
int main() { if (LDAP_VENDOR_VERSION < 20300) exit(1); else exit(0); }
		      ],
		      [AC_MSG_RESULT([yes])],
		      [
			AC_MSG_RESULT([no])
			AC_MSG_ERROR([OpenLDAP is not v2.3 or later])
		      ],
		      [AC_MSG_RESULT([yes])])
          # warning: if ${withval}/lib isn't in the shared library
          # search path, then adding -lldap may cause AC_TRY_RUN tests
          # below to fail
		  LDFLAGS="-L${withval}/lib $LDFLAGS"
		  AC_CHECK_HEADERS(ldap.h)
		  AC_CHECK_LIB(lber, ber_free, ,
				     AC_MSG_ERROR([ber_free not found in liblber]) )
		  AC_CHECK_LIB(ldap, ldap_sasl_bind_s, ,
				     AC_MSG_ERROR([ldap_sasl_bind_s not found in libldap]) )
		  AC_CHECK_FUNC(ldap_search_ext_s, ,
				     AC_MSG_ERROR([ldap_search_ext_s not found in libldap]) )
		  AC_CHECK_FUNC(ldap_str2dn, ,
				     AC_MSG_ERROR([ldap_str2dn not found in libldap]) )
		  AC_CHECK_FUNC(ldap_start_tls_s, ,
				     AC_MSG_ERROR([ldap_start_tls_s not found in libldap]) )
               fi
       ]
)

dnl
dnl Check for VOMS libraries
dnl

AC_ARG_WITH(voms,
    AS_HELP_STRING([--with-voms=PATH],[Build with VOMS support]),
	[
		if test "x$withval" = "xyes" ; then
		   AC_MSG_ERROR([--with-voms requires PATH argument])
		fi
		if test "x$withval" != "xno" ; then
		   CPPFLAGS="-I${withval}/include -I${withval}/include/voms -I${withval}/include/glite/security/voms $CPPFLAGS"
		   LDFLAGS="-L${withval}/lib -L${withval}/lib64 $LDFLAGS"
		   SAVE_LIBS="$LIBS"
		   AC_CHECK_HEADER(voms_apic.h)
		   AC_CHECK_HEADER(newformat.h)
                   AC_SEARCH_LIBS(VOMS_Init, vomsapi vomsc, ,
			     AC_MSG_ERROR([VOMS_Init not found in libvomsapi/libvomsc]) )
                   VOMS_LIBS="$LIBS"
                   LIBS="$SAVE_LIBS"
                   AC_SUBST([VOMS_LIBS])
                   HAVE_VOMS=1
                   AC_DEFINE(HAVE_VOMS)
		fi
	]
)

AM_CONDITIONAL([HAVE_VOMS], [test x"$HAVE_VOMS" = x1])

AC_CONFIG_FILES([
	Makefile
	web/Makefile
	systemd/Makefile
	man/Makefile
	myproxy.h
	myproxy.pc])
AC_OUTPUT
