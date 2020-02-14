ACLOCAL_AMFLAGS=-I m4

SUBDIRS = attr context logging python .  xio_driver test
pkgconfigdir = $(libdir)/pkgconfig

doc_DATA = GLOBUS_LICENSE
pkgconfig_DATA = globus-net-manager.pc
include_HEADERS = globus_net_manager.h attr/globus_net_manager_attr.h
lib_LTLIBRARIES = libglobus_net_manager.la

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_builddir) -I$(top_srcdir)/attr -I$(top_srcdir)/context -I$(top_srcdir)/logging $(PACKAGE_DEP_CFLAGS)
if ENABLE_PYTHON
AM_CPPFLAGS += -DENABLE_PYTHON -I$(top_srcdir)/python
python_libadd = python/libglobus_net_manager_python.la
endif

libglobus_net_manager_la_LIBADD = \
        attr/libglobus_net_manager_attr.la \
        context/libglobus_net_manager_context.la \
        logging/libglobus_net_manager_logging.la \
        $(python_libadd) \
        $(PACKAGE_DEP_LIBS)
libglobus_net_manager_la_SOURCES = module.c register.c unregister.c
libglobus_net_manager_la_LDFLAGS = \
	-version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined

EXTRA_DIST = dirt.sh version.h.in globus-vararg-enums-doxygen-filter.pl $(doc_DATA)

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_common_la_SOURCES)
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
		$(INSTALL) -m 644 doc/man/man3/[Gg][Ll][Oo][Bb][Uu][Ss]*.3 $(DESTDIR)$(mandir)/man3  && \
		rm -f $(DESTDIR)$(mandir)/man3/globus_xio_attr_cntl.3 ; \
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
