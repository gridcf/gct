ACLOCAL_AMFLAGS=-I m4
SUBDIRS = scripts . test

libexecdatadir = $(libexecdir)
pkgconfigdir = $(libdir)/pkgconfig

BUILT_SOURCES = globus_gram_protocol_constants.h

EXTRA_DIST = globus-gram-protocol-constants.sh \
	     globus_gram_protocol.dox \
	     $(doc_DATA) \
	     dirt.sh
include_HEADERS = \
	globus_gram_protocol_constants.h \
	globus_gram_protocol.h 
noinst_HEADERS = globus_i_gram_protocol.h
libexecdata_DATA = globus-gram-protocol-constants.sh
doc_DATA = GLOBUS_LICENSE
lib_LTLIBRARIES = libglobus_gram_protocol.la
pkgconfig_DATA = globus-gram-protocol.pc

globus_gram_protocol_constants.h: globus-gram-protocol-constants.sh
	$(RM) globus_gram_protocol_constants.h
	$(PERL) $(srcdir)/scripts/create_protocol_constants.pl -header \
		$(srcdir)/globus-gram-protocol-constants.sh

MAINTAINERCLEANFILES: globus_gram_protocol_constants.h

libglobus_gram_protocol_la_CPPFLAGS = $(PACKAGE_DEP_CFLAGS)
libglobus_gram_protocol_la_LDFLAGS = \
        -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined
libglobus_gram_protocol_la_LIBADD = $(PACKAGE_DEP_LIBS)
libglobus_gram_protocol_la_SOURCES = \
	globus_gram_protocol.c \
	globus_gram_protocol.h \
	globus_gram_protocol_constants.h \
	globus_gram_protocol_io.c \
	globus_gram_protocol_frame.c \
	globus_gram_protocol_error.c \
	globus_gram_protocol_pack.c

Makefile: $(BUILT_SOURCES)

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_gram_protocol_la_SOURCES) globus_gram_protocol.dox
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
