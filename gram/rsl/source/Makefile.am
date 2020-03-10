ACLOCAL_AMFLAGS=-I m4
SUBDIRS = . test

pkgconfigdir = $(libdir)/pkgconfig

doc_DATA = GLOBUS_LICENSE
include_HEADERS = globus_rsl.h globus_rsl_assist.h
lib_LTLIBRARIES = libglobus_rsl.la libglobus_rsl_assist.la
pkgconfig_DATA = globus-rsl.pc

BUILT_SOURCES = globus_rsl_scanner.c globus_rsl_scanner.h globus_rsl_parser.c globus_rsl_parser.h
AM_CPPFLAGS = $(PACKAGE_DEP_CFLAGS)
AM_YFLAGS = -d -p globus_rsl -o globus_rsl_parser.c
AM_LFLAGS = -s -P globus_rsl --bison-bridge -o globus_rsl_scanner.c

libglobus_rsl_la_SOURCES = \
	globus_rsl.c \
	globus_rsl.h \
        globus_i_rsl_parser.h \
	globus_rsl_scanner.l \
	globus_rsl_parser.y \
	globus_rsl_assist.h \
	globus_rsl_assist.c
libglobus_rsl_la_LDFLAGS = \
        -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined
libglobus_rsl_la_LIBADD = $(PACKAGE_DEP_LIBS)

libglobus_rsl_assist_la_SOURCES = dummy.c
libglobus_rsl_assist_la_LDFLAGS = \
        -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined
libglobus_rsl_assist_la_LIBADD = libglobus_rsl.la $(PACKAGE_DEP_LIBS)

EXTRA_DIST = \
        dirt.sh \
        globus_rsl_scanner.h \
	$(doc_DATA)

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_rsl_la_SOURCES)
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
