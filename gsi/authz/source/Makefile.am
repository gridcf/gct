ACLOCAL_AMFLAGS=-I m4
SUBDIRS = . test

pkgconfigdir = $(libdir)/pkgconfig

doc_DATA = GLOBUS_LICENSE
include_HEADERS = globus_gsi_authz.h
lib_LTLIBRARIES = libglobus_authz.la
pkgconfig_DATA = globus-authz.pc

libglobus_authz_la_CPPFLAGS = $(PACKAGE_DEP_CFLAGS)
libglobus_authz_la_LDFLAGS = \
        -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined
libglobus_authz_la_LIBADD = $(PACKAGE_DEP_LIBS)
libglobus_authz_la_SOURCES = \
	globus_gsi_authz.c \
	globus_gsi_authz_error.c \
	globus_gsi_authz.h \
	globus_gsi_authz_constants.h \
	globus_i_gsi_authz.h

EXTRA_DIST = dirt.sh $(doc_DATA)

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_authz_la_SOURCES)
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
