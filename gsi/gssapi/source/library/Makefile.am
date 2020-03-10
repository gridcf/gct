include_HEADERS = gssapi.h
lib_LTLIBRARIES = libglobus_gssapi_gsi.la

if USE_SYMBOL_LABELS
LABELFLAGS = -Wl,--version-script,$(srcdir)/gssapi.sym
endif

gridsecurityconfdir = $(sysconfdir)/grid-security
gridsecurityconf_DATA = gsi.conf

libglobus_gssapi_gsi_la_SOURCES = \
	accept_sec_context.c \
	acquire_cred.c \
	compare_name.c \
	delete_sec_context.c \
	display_name.c \
	duplicate_name.c \
	display_status.c \
	export_name.c \
	export_sec_context.c \
	get_mic.c \
	get_hash.c \
	gmac.c \
	import_name.c \
	import_sec_context.c \
	init_sec_context.c \
	inquire_cred.c \
	inquire_context.c \
        inquire_names_for_mech.c \
	oid_functions.c \
	release_cred.c \
	release_name.c \
	unwrap.c \
	verify_mic.c \
	wrap.c \
	release_buffer.c \
	module.c \
	gssapi_working.c \
	gssapi.h \
	gssapi_openssl.h \
	globus_i_gsi_gss_utils.h \
        globus_i_gsi_gss_utils.c \
        globus_gsi_gss_constants.h \
        globus_gsi_gss_error.c \
	accept_delegation.c \
	import_cred.c \
	export_cred.c \
	init_delegation.c \
	inquire_cred_by_oid.c \
	inquire_sec_context_by_oid.c \
	buffer_set.c \
	set_sec_context_option.c \
	globus_ssl_locl.h \
        read_vhost_cred_dir.c

libglobus_gssapi_gsi_la_CPPFLAGS = -I$(top_builddir) $(PACKAGE_DEP_CFLAGS) $(OPENSSL_CFLAGS)
libglobus_gssapi_gsi_la_LIBADD = $(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS)
libglobus_gssapi_gsi_la_LDFLAGS = $(LABELFLAGS) \
	-version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
	-no-undefined
nodist_libglobus_gssapi_gsi_la_SOURCES = gsi.conf.h
BUILT_SOURCES = gsi.conf.h

gsi.conf.h: Makefile $(srcdir)/gsi.conf
	$(AWK) 'BEGIN { print "static char *globus_l_gsi_conf_string  =" } \
	              { print "\"" $$0 "\\n\"" } \
	        END { print ";" }' < $(srcdir)/gsi.conf > $(builddir)/gsi.conf.h


CLEANFILES = gsi.conf.h
EXTRA_DIST = gssapi.sym gsi.conf

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_gssapi_gsi_la_SOURCES)
	rm -rf doc
	$(DOXYGEN)
	touch $@

clean-local:
	rm -rf doc doc-stamp
endif

all-local: $(DOC_STAMPS)

install-data-local: $(DOC_STAMPS)
	if test -d doc/man/man3; then \
		install -d -m 755 $(DESTDIR)$(mandir)/man3 && \
		$(INSTALL) -m 644 doc/man/man3/[Gg][Ll][Oo][Bb][Uu][Ss]*.3 $(DESTDIR)$(mandir)/man3 ; \
	fi
	if test -d doc/html; then \
		for dir in `cd doc; find html -type d`; do \
			install -d -m 755 $(DESTDIR)$(docdir)/$$dir || exit 1; \
		done ; \
		for f in `cd doc; find html -type f`; do \
			$(INSTALL) -m 644 doc/$$f $(DESTDIR)$(docdir)/$$f || exit 1; \
		done ; \
	fi
