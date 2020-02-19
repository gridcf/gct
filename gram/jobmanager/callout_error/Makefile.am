ACLOCAL_AMFLAGS=-I m4

pkgconfigdir = $(libdir)/pkgconfig

doc_DATA = GLOBUS_LICENSE
include_HEADERS = globus_gram_jobmanager_callout_error.h
lib_LTLIBRARIES = libglobus_gram_job_manager_callout_error.la
pkgconfig_DATA = globus-gram-job-manager-callout-error.pc

libglobus_gram_job_manager_callout_error_la_CPPFLAGS = $(PACKAGE_DEP_CFLAGS)
libglobus_gram_job_manager_callout_error_la_LIBADD = $(PACKAGE_DEP_LIBS)
libglobus_gram_job_manager_callout_error_la_LDFLAGS = \
        -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined
libglobus_gram_job_manager_callout_error_la_SOURCES = \
	globus_i_gram_jobmanager_callout_error.c \
	globus_gram_jobmanager_callout_error.h

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_gram_job_manager_callout_error_la_SOURCES)
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

EXTRA_DIST = dirt.sh $(doc_DATA)

distuninstallcheck:
	@:
