ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libglobus_gridmap_callout.la
doc_DATA = GLOBUS_LICENSE
sysconf_DATA = gridmap_callout-gsi_authz.conf

libglobus_gridmap_callout_la_SOURCES = globus_gridmap_callout.c
libglobus_gridmap_callout_la_CPPFLAGS = $(PACKAGE_DEP_CFLAGS)
libglobus_gridmap_callout_la_LIBADD = $(PACKAGE_DEP_LIBS)
libglobus_gridmap_callout_la_LDFLAGS = \
        -avoid-version \
        -no-undefined \
        -module \
        -rpath $(libdir)
EXTRA_DIST= dirt.sh $(doc_DATA) $(sysconf_DATA)

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_gridmap_callout_la_SOURCES)
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
