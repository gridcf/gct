ACLOCAL_AMFLAGS=-I m4
SUBDIRS = . programs test

pkgconfigdir = $(libdir)/pkgconfig

doc_DATA = GLOBUS_LICENSE
include_HEADERS= \
	globus_gss_assist.h \
        globus_gss_assist_constants.h
lib_LTLIBRARIES = libglobus_gss_assist.la
pkgconfig_DATA = globus-gss-assist.pc

libglobus_gss_assist_la_CPPFLAGS = $(PACKAGE_DEP_CFLAGS)
libglobus_gss_assist_la_LIBADD = $(PACKAGE_DEP_LIBS)
libglobus_gss_assist_la_LDFLAGS = \
	-version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
	-no-undefined
libglobus_gss_assist_la_SOURCES = \
	accept.c \
	acquire.c \
	display.c \
	hostname.c \
	globus_gss_assist_constants.h \
	globus_gss_assist_error.c \
	globus_i_gss_assist.h \
	globus_gss_assist_module.c \
	globus_gss_assist.h \
	gridmap.c \
	import_sec_context.c \
	export_sec_context.c \
	init.c \
	read_vhost_cred_dir.c \
	tokens_f.c \
	unwrap.c \
	wrap.c \
	set_sec_context_opts.c 

EXTRA_DIST = \
       dirt.sh \
       $(doc_DATA)

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_gss_assist_la_SOURCES)
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
		for dir in `(cd doc; find html -type d)`; do \
			install -d -m 755 $(DESTDIR)$(docdir)/$$dir || exit 1; \
		done ; \
		for f in `(cd doc; find html -type f)`; do \
			$(INSTALL) -m 644 doc/$$f $(DESTDIR)$(docdir)/$$f  || exit 1; \
		done ; \
	fi

distuninstallcheck:
	@:
