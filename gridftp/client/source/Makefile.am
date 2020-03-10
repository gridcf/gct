ACLOCAL_AMFLAGS=-I m4
SUBDIRS = . test

pkgconfigdir = $(libdir)/pkgconfig

libexec_SCRIPTS = gridftp-ssh
include_HEADERS = \
	globus_ftp_client_debug_plugin.h \
	globus_ftp_client_restart_plugin.h \
	globus_ftp_client_restart_marker_plugin.h \
	globus_ftp_client_perf_plugin.h \
	globus_ftp_client_throughput_plugin.h \
	globus_ftp_client.h \
	globus_ftp_client_plugin.h
lib_LTLIBRARIES = libglobus_ftp_client.la
doc_DATA = GLOBUS_LICENSE
pkgconfig_DATA = globus-ftp-client.pc

AM_CPPFLAGS = $(PACKAGE_DEP_CFLAGS) $(OPENSSL_CFLAGS) 
libglobus_ftp_client_la_SOURCES = \
	globus_ftp_client_debug_plugin.h \
	globus_ftp_client_restart_plugin.h \
	globus_ftp_client_restart_marker_plugin.h \
	globus_ftp_client_perf_plugin.h \
	globus_ftp_client_throughput_plugin.h \
	globus_ftp_client_plugin.h \
	globus_i_ftp_client.h \
	globus_ftp_client_attr.c \
	globus_ftp_client.c \
	globus_ftp_client_error.c \
	globus_ftp_client_data.c \
	globus_ftp_client_exists.c \
	globus_ftp_client_debug_plugin.c \
	globus_ftp_client_restart_plugin.c \
	globus_ftp_client_restart_marker_plugin.c \
	globus_ftp_client_perf_plugin.c \
	globus_ftp_client_throughput_plugin.c \
	globus_ftp_client_handle.c \
	globus_ftp_client_plugin.c \
	globus_ftp_client_restart.c \
	globus_ftp_client_restart_marker.c \
	globus_ftp_client_state.c \
	globus_ftp_client_transfer.c \
	globus_ftp_client_feature.c 
libglobus_ftp_client_la_LDFLAGS = \
        -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined
libglobus_ftp_client_la_LIBADD = $(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS) 


EXTRA_DIST = \
        dirt.sh \
        globus_ftp_client_restart_plugin.example \
	globus_ftp_client_debug_plugin.example \
	globus_ftp_client_restart_marker_plugin.example \
	globus_ftp_client_throughput_nl_plugin.h \
	globus_ftp_client_throughput_nl_plugin.c \
	$(doc_DATA)

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_ftp_client_la_SOURCES)
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

distuninstallcheck:
	@:
