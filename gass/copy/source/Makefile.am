ACLOCAL_AMFLAGS=-I m4
SUBDIRS = . test
pkgconfigdir = $(libdir)/pkgconfig

doc_DATA = GLOBUS_LICENSE
man_MANS = globus-url-copy.1
include_HEADERS = \
	globus_gass_copy.h \
	globus_url_copy.h
lib_LTLIBRARIES = libglobus_gass_copy.la
bin_PROGRAMS = globus-url-copy 
pkgconfig_DATA = globus-gass-copy.pc
check_PROGRAMS = globus-url-copy-noinst

AM_CPPFLAGS = $(PACKAGE_DEP_CFLAGS) $(OPENSSL_CFLAGS)

libglobus_gass_copy_la_SOURCES = \
	globus_gass_copy.c   \
	globus_gass_copy_glob.c \
	globus_i_gass_copy.h \
	globus_url_copy.h \
	globus_gass_copy.h 
libglobus_gass_copy_la_LDFLAGS = \
        -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined
libglobus_gass_copy_la_LIBADD = $(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS)

globus_url_copy_SOURCES = globus_url_copy.c
globus_url_copy_LDFLAGS = -export-dynamic -dlpreopen force
globus_url_copy_LDADD = $(GSI_DRIVER_DLOPEN) $(POPEN_DRIVER_DLOPEN) \
                        libglobus_gass_copy.la \
			$(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS) -lltdl
globus_url_copy_noinst_SOURCES = globus_url_copy.c
globus_url_copy_noinst_LDFLAGS = -dlpreopen force
globus_url_copy_noinst_LDADD = $(GSI_DRIVER_DLPREOPEN) \
                        libglobus_gass_copy.la \
			$(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS) -lltdl

MANUAL_SOURCE = globus-url-copy.txt

if BUILD_MANPAGES
SUFFIXES = .txt .1
.txt.1:
	$(A2X) -f manpage -d manpage $<

MAINTAINERCLEANFILES = $(man_MANS)
endif

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_gass_copy_la_SOURCES)
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

EXTRA_DIST = \
	dirt.sh \
	$(doc_DATA) \
        $(man_MANS) \
        $(MANUAL_SOURCE) \
        globus-url-copy.txt


distuninstallcheck:
	@:
