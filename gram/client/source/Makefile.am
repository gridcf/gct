ACLOCAL_AMFLAGS=-I m4
SUBDIRS = . test

pkgconfigdir = $(libdir)/pkgconfig

include_HEADERS = globus_gram_client.h
lib_LTLIBRARIES = libglobus_gram_client.la
doc_DATA = GLOBUS_LICENSE
pkgconfig_DATA = globus-gram-client.pc

libglobus_gram_client_la_CPPFLAGS = $(PACKAGE_DEP_CFLAGS)
libglobus_gram_client_la_LIBADD = $(PACKAGE_DEP_LIBS)
libglobus_gram_client_la_LDFLAGS = \
        -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined
libglobus_gram_client_la_SOURCES = \
	globus_gram_client.c \
        globus_gram_client_attr.c \
        globus_i_gram_client.h \
	globus_gram_client.h

EXTRA_DIST = \
	dirt.sh resource_manager_contact.dox \
	$(doc_DATA)

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_gram_client_la_SOURCES)
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
