## Process this file with automake to produce Makefile.in
SUBDIRS= \
	web \
	systemd \
	man

LibSources= \
	accept_credmap.c \
	accept_credmap.h \
	auth_pam.c \
	auth_pam.h \
	certauth_extensions.c \
	certauth_extensions.h \
	certauth_resolveuser.c \
	certauth_resolveuser.h \
	getopt_long.c \
	gsi_socket.c \
	gsi_socket.h \
	gsi_socket_priv.h \
	gssapi.c \
	myproxy.c \
	myproxy_authorization.c \
	myproxy_authorization.h \
	myproxy_common.h \
	myproxy_creds.c \
	myproxy_creds.h \
	myproxy_delegation.c \
	myproxy_delegation.h \
	myproxy_extensions.c \
	myproxy_extensions.h \
	myproxy_read_pass.c \
	myproxy_read_pass.h \
	myproxy_log.c \
	myproxy_log.h \
	myproxy_ocsp.c \
	myproxy_ocsp.h \
	myproxy_ocsp_aia.c \
	myproxy_ocsp_aia.h \
	myproxy_popen.c \
	myproxy_popen.h \
	myproxy_sasl_client.c \
	myproxy_sasl_client.h \
	myproxy_sasl_server.h \
	myproxy_sasl_server.c \
	myproxy_server.h \
	myproxy_server_config.c \
	pidfile.c \
	plugin_common.c \
	plugin_common.h \
	port_getopt.h \
	safe_id_range_list.c \
	safe_id_range_list.h \
	safe_is_path_trusted.c \
	safe_is_path_trusted.h \
	ssl_utils.c \
	ssl_utils.h \
	string_funcs.c \
	string_funcs.h \
	verror.c \
	verror.h \
	vomsclient.h \
	voms_utils.c \
	voms_utils.h \
	vparse.c \
	vparse.h

Scripts= \
	myproxy-test \
	myproxy-replicate \
	myproxy-test-replicate \
	myproxy-admin-adduser \
	myproxy-admin-addservice \
	myproxy-server-setup

sbin_SCRIPTS = $(Scripts)

check_SCRIPTS = myproxy-test-wrapper

nodist_include_HEADERS = \
	myproxy.h
include_HEADERS = \
	myproxy_constants.h \
	myproxy_authorization.h \
	myproxy_protocol.h \
	myproxy_creds.h \
	myproxy_delegation.h \
	myproxy_log.h \
	myproxy_read_pass.h \
	myproxy_sasl_client.h \
	myproxy_sasl_server.h \
	myproxy_server.h \
	verror.h

ACLOCAL_AMFLAGS = -I m4
AM_CPPFLAGS = $(GLOBUS_CFLAGS)
LDADD = $(GLOBUS_LIBS)

lib_LTLIBRARIES = libmyproxy.la

if HAVE_VOMS
lib_LTLIBRARIES += libmyproxy_voms.la
else
EXTRA_LTLIBRARIES = libmyproxy_voms.la
endif

libmyproxy_la_SOURCES = $(LibSources)
libmyproxy_la_LDFLAGS = \
	-version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION)
libmyproxy_la_LIBADD = $(GLOBUS_LIBS)

libmyproxy_voms_la_SOURCES = gsi_socket_voms.c vomsclient.c
libmyproxy_voms_la_LDFLAGS = \
	-module -avoid-version -no-undefined
libmyproxy_voms_la_LIBADD = libmyproxy.la $(VOMS_LIBS) $(GLOBUS_LIBS)

bin_PROGRAMS= \
	myproxy-init \
	myproxy-info \
        myproxy-store \
	myproxy-destroy \
        myproxy-retrieve \
	myproxy-get-trustroots \
	myproxy-get-delegation \
	myproxy-logon \
	myproxy-change-pass-phrase

sbin_PROGRAMS= \
	myproxy-server \
	myproxy-admin-load-credential \
	myproxy-admin-query \
	myproxy-admin-change-pass

myproxy_init_SOURCES = 	myproxy_init.c
myproxy_init_LDADD = ./libmyproxy.la

myproxy_info_SOURCES = 	myproxy_info.c
myproxy_info_LDADD = ./libmyproxy.la

myproxy_store_SOURCES = myproxy_store.c
myproxy_store_LDADD = ./libmyproxy.la $(LDADD)

myproxy_retrieve_SOURCES = myproxy_get_credential.c
myproxy_retrieve_LDADD = ./libmyproxy.la $(LDADD)

myproxy_destroy_SOURCES = myproxy_destroy.c
myproxy_destroy_LDADD = ./libmyproxy.la

myproxy_server_SOURCES = myproxy_server.c
myproxy_server_LDADD = ./libmyproxy.la $(LDADD)

myproxy_get_trustroots_SOURCES = myproxy_get_trustroots.c
myproxy_get_trustroots_LDADD = ./libmyproxy.la

myproxy_get_delegation_SOURCES = myproxy_get_delegation.c
myproxy_get_delegation_LDADD = ./libmyproxy.la $(LDADD)

myproxy_logon_SOURCES = myproxy_get_delegation.c
myproxy_logon_LDADD = ./libmyproxy.la $(LDADD)

myproxy_change_pass_phrase_SOURCES = myproxy_cp.c
myproxy_change_pass_phrase_LDADD = ./libmyproxy.la

myproxy_admin_query_SOURCES = myproxy_arq.c
myproxy_admin_query_LDADD = ./libmyproxy.la

myproxy_admin_load_credential_SOURCES = myproxy_alcf.c
myproxy_admin_load_credential_LDADD = ./libmyproxy.la

myproxy_admin_change_pass_SOURCES = myproxy_acp.c
myproxy_admin_change_pass_LDADD = ./libmyproxy.la

pkgdata_DATA = \
               LICENSE LICENSE.sasl LICENSE.netbsd LICENSE.pidfile \
               LICENSE.safefile \
               VERSION PROTOCOL README.sasl REPOSITORY \
               myproxy-server.config \
               etc.services.modifications etc.inetd.conf.modifications \
               myproxy.cron myproxy-crl.cron myproxy-get-trustroots.cron \
               etc.init.d.myproxy etc.init.d.myproxy.nonroot \
               etc.xinetd.myproxy myproxy-passphrase-policy \
               myproxy-certificate-mapapp myproxy-revoke \
               myproxy-accepted-credentials-mapapp \
               myproxy-cert-checker myproxy-certreq-checker

EXTRA_DIST = $(Scripts) $(check_SCRIPTS) \
               LICENSE LICENSE.sasl LICENSE.netbsd LICENSE.pidfile \
               LICENSE.safefile \
               VERSION PROTOCOL README.sasl REPOSITORY \
               myproxy-server.config \
               etc.services.modifications etc.inetd.conf.modifications \
               myproxy.cron myproxy-crl.cron myproxy-get-trustroots.cron \
               etc.init.d.myproxy etc.init.d.myproxy.nonroot \
               etc.xinetd.myproxy myproxy-passphrase-policy \
               myproxy-certificate-mapapp myproxy-revoke \
               myproxy-accepted-credentials-mapapp \
               myproxy-cert-checker myproxy-certreq-checker \
               myproxy.init myproxy.init.sles myproxy.sysconfig \
               README.Fedora

myproxy-admin-addservice: myproxy-admin-adduser
	rm -f myproxy-admin-addservice
	$(LN_S) $(srcdir)/myproxy-admin-adduser myproxy-admin-addservice

KRB5CPPFLAGS = @KRB5CPPFLAGS@

gssapi.lo: gssapi.c plugin_common.h
plugin_common.lo: plugin_common.c plugin_common.h

splint:
	splint +posixlib \
	-Dsocklen_t=int -Din_addr_t=int \
	-Du_int="unsigned int" -Du_char="unsigned char" \
	$(DEFS) *.c

pkgconfdir = ${libdir}/pkgconfig

pkgconf_DATA = myproxy.pc
if ENABLE_TESTS
TESTS_ENVIRONMENT = export GSI_PROXY_UTILS_PATH="$(GSI_PROXY_UTILS_PATH)"; export GSI_CERT_UTILS_PATH="$(GSI_CERT_UTILS_PATH)"; export PATH="$(abs_srcdir)$${GSI_PROXY_UTILS_PATH:+:$$GSI_PROXY_UTILS_PATH}$${GSI_CERT_UTILS_PATH:+:$$GSI_CERT_UTILS_PATH}:$(abs_builddir):$${PATH}"; export OPENSSL="$(OPENSSL)";
TESTS = myproxy-test-wrapper
endif
