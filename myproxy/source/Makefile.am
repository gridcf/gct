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
	gssapi.c \
	myproxy.c \
	myproxy.h \
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
	myproxy_usage.h \
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
	vomsclient.c \
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
	myproxy-server-setup \
        myproxy-test-wrapper

sbin_SCRIPTS = $(Scripts)

include_HEADERS= \
	myproxy.h \
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

lib_LTLIBRARIES = libmyproxy$(_GLOBUS_FLAVOR_NAME).la

libmyproxy___GLOBUS_FLAVOR_NAME__la_SOURCES = $(LibSources)

libmyproxy___GLOBUS_FLAVOR_NAME__la_LDFLAGS = $(GPT_LDFLAGS) \
	-version-info ${MAJOR_VERSION}:${MINOR_VERSION}:${AGE_VERSION}

libmyproxy___GLOBUS_FLAVOR_NAME__la_LIBADD = $(GLOBUS_LIBS)

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

myproxy_init_LDFLAGS = $(GPT_LDFLAGS)

myproxy_init_LDADD = ./libmyproxy$(_GLOBUS_FLAVOR_NAME).la

myproxy_info_SOURCES = 	myproxy_info.c

myproxy_info_LDFLAGS = $(GPT_LDFLAGS)

myproxy_info_LDADD = ./libmyproxy$(_GLOBUS_FLAVOR_NAME).la

myproxy_store_SOURCES = myproxy_store.c

myproxy_store_LDFLAGS = $(GPT_LDFLAGS)

myproxy_store_LDADD = ./libmyproxy$(_GLOBUS_FLAVOR_NAME).la $(LDADD)

myproxy_retrieve_SOURCES = myproxy_get_credential.c

myproxy_retrieve_LDFLAGS = $(GPT_LDFLAGS)

myproxy_retrieve_LDADD = ./libmyproxy$(_GLOBUS_FLAVOR_NAME).la $(LDADD)

myproxy_destroy_SOURCES = myproxy_destroy.c

myproxy_destroy_LDFLAGS = $(GPT_LDFLAGS)

myproxy_destroy_LDADD = ./libmyproxy$(_GLOBUS_FLAVOR_NAME).la

myproxy_server_SOURCES = myproxy_server.c myproxy_usage.c
myproxy_server_LDFLAGS = $(GPT_LDFLAGS)

myproxy_server_LDADD = ./libmyproxy$(_GLOBUS_FLAVOR_NAME).la $(LDADD)

myproxy_get_trustroots_SOURCES = myproxy_get_trustroots.c

myproxy_get_trustroots_LDFLAGS = $(GPT_LDFLAGS)

myproxy_get_trustroots_LDADD = ./libmyproxy$(_GLOBUS_FLAVOR_NAME).la

myproxy_get_delegation_SOURCES = myproxy_get_delegation.c

myproxy_get_delegation_LDFLAGS = $(GPT_LDFLAGS)

myproxy_get_delegation_LDADD = ./libmyproxy$(_GLOBUS_FLAVOR_NAME).la $(LDADD)

myproxy_logon_SOURCES = myproxy_get_delegation.c

myproxy_logon_LDFLAGS = $(GPT_LDFLAGS)

myproxy_logon_LDADD = ./libmyproxy$(_GLOBUS_FLAVOR_NAME).la $(LDADD)

myproxy_change_pass_phrase_SOURCES = myproxy_cp.c

myproxy_change_pass_phrase_LDFLAGS = $(GPT_LDFLAGS)

myproxy_change_pass_phrase_LDADD = ./libmyproxy$(_GLOBUS_FLAVOR_NAME).la

myproxy_admin_query_SOURCES = myproxy_arq.c

myproxy_admin_query_LDFLAGS = $(GPT_LDFLAGS)

myproxy_admin_query_LDADD = ./libmyproxy$(_GLOBUS_FLAVOR_NAME).la

myproxy_admin_load_credential_SOURCES = myproxy_alcf.c

myproxy_admin_load_credential_LDFLAGS = $(GPT_LDFLAGS)

myproxy_admin_load_credential_LDADD = ./libmyproxy$(_GLOBUS_FLAVOR_NAME).la

myproxy_admin_change_pass_SOURCES = myproxy_acp.c

myproxy_admin_change_pass_LDFLAGS = $(GPT_LDFLAGS)

myproxy_admin_change_pass_LDADD = ./libmyproxy$(_GLOBUS_FLAVOR_NAME).la

pkgdata_DATA = README INSTALL myproxy-server.config \
               LICENSE LICENSE.sasl LICENSE.netbsd LICENSE.pidfile \
               LICENSE.safefile LICENSE.globus \
               VERSION PROTOCOL \
               etc.services.modifications etc.inetd.conf.modifications \
               myproxy.cron myproxy-crl.cron myproxy-get-trustroots.cron \
               etc.init.d.myproxy etc.init.d.myproxy.nonroot \
               etc.xinetd.myproxy myproxy-passphrase-policy \
               myproxy-certificate-mapapp myproxy-revoke \
               myproxy-accepted-credentials-mapapp \
               myproxy-cert-checker myproxy-certreq-checker

EXTRA_DIST = $(Scripts) LICENSE LICENSE.sasl LICENSE.netbsd LICENSE.pidfile \
               LICENSE.safefile LICENSE.globus \
               VERSION PROTOCOL \
               myproxy-server.config \
               etc.services.modifications etc.inetd.conf.modifications \
               myproxy.cron myproxy-crl.cron myproxy-get-trustroots.cron \
               etc.init.d.myproxy etc.init.d.myproxy.nonroot \
               etc.xinetd.myproxy myproxy-passphrase-policy \
               myproxy-certificate-mapapp myproxy-revoke \
               myproxy-accepted-credentials-mapapp \
               myproxy-cert-checker myproxy-certreq-checker \
               myproxy.init myproxy.init.sles myproxy.sysconfig \
               README.Fedora \
               myproxy.spec pkgdata/pkg_data_src.gpt.in m4/myproxy.m4

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
	$(DEFS) $(GPT_INCLUDES) *.c

pkgconfdir = ${libdir}/pkgconfig

pkgconf_DATA = myproxy.pc
TESTS_ENVIRONMENT = PATH="$(abs_srcdir):$(abs_builddir):$${PATH}";
TESTS = myproxy-test-wrapper