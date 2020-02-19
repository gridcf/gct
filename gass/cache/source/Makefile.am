ACLOCAL_AMFLAGS=-I m4
pkgconfigdir = $(libdir)/pkgconfig

doc_DATA = GLOBUS_LICENSE
include_HEADERS = globus_gass_cache.h 
lib_LTLIBRARIES = libglobus_gass_cache.la
pkgconfig_DATA = globus-gass-cache.pc

libglobus_gass_cache_la_CPPFLAGS = $(PACKAGE_DEP_CFLAGS) $(OPENSSL_CFLAGS)
libglobus_gass_cache_la_LDFLAGS = \
        -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined
libglobus_gass_cache_la_LIBADD = $(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS)
libglobus_gass_cache_la_SOURCES = \
	globus_gass_cache.c \
	globus_gass_cache_config.c \
	globus_gass_cache.h \
	globus_i_gass_cache.h \
	globus_i_gass_cache_config.h

EXTRA_DIST = dirt.sh $(doc_DATA)

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_gass_cache_la_SOURCES)
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
			$(INSTALL) -m 644 doc/$$f $(DESTDIR)$(docdir)/$$f || exit 1; \
		done ; \
	fi

distuninstallcheck:
	@:
