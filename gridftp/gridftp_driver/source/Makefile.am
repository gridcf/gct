ACLOCAL_AMFLAGS=-I m4
SUBDIRS = . test
pkgconfigdir = $(libdir)/pkgconfig

doc_DATA = GLOBUS_LICENSE
include_HEADERS = globus_xio_gridftp_driver.h
lib_LTLIBRARIES = libglobus_xio_gridftp_driver.la
pkgconfig_DATA = globus-xio-gridftp-driver.pc

libglobus_xio_gridftp_driver_la_CPPFLAGS = $(PACKAGE_DEP_CFLAGS)
libglobus_xio_gridftp_driver_la_LIBADD = $(PACKAGE_DEP_LIBS)
libglobus_xio_gridftp_driver_la_LDFLAGS = \
        -avoid-version \
        -no-undefined \
	-module -rpath $(libdir)
libglobus_xio_gridftp_driver_la_SOURCES = globus_xio_gridftp_driver.c

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_xio_gridftp_driver_la_SOURCES)
	rm -rf doc
	$(DOXYGEN)
	touch $@

clean-local:
	rm -rf doc doc-stamp
endif

all-local: $(DOC_STAMPS)

# Only install manpages that are prefixed by globus
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

EXTRA_DIST = dirt.sh $(doc_DATA) \
    globus-vararg-enums-doxygen-filter.pl

distuninstallcheck:
	@:
