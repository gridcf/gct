ACLOCAL_AMFLAGS=-I m4
SUBDIRS = init config . test

pkgconfigdir = $(libdir)/pkgconfig

include_HEADERS = \
        globus_scheduler_event_generator.h \
        globus_scheduler_event_generator_app.h

lib_LTLIBRARIES = libglobus_scheduler_event_generator.la
sbin_PROGRAMS = globus-scheduler-event-generator
sbin_SCRIPTS = globus-scheduler-event-generator-admin
doc_DATA = GLOBUS_LICENSE
pkgconfig_DATA = globus-scheduler-event-generator.pc
man_MANS = \
        globus-scheduler-event-generator.8 \
        globus-scheduler-event-generator-admin.8

libglobus_scheduler_event_generator_la_CPPFLAGS = $(PACKAGE_DEP_CFLAGS)
libglobus_scheduler_event_generator_la_LIBADD = $(PACKAGE_DEP_LIBS)
libglobus_scheduler_event_generator_la_LDFLAGS = \
        -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined
libglobus_scheduler_event_generator_la_SOURCES = \
	globus_scheduler_event_generator.h globus_scheduler_event_generator.c

globus_scheduler_event_generator_CPPFLAGS = $(PACKAGE_DEP_CFLAGS)
globus_scheduler_event_generator_SOURCES = \
	main.c \
	globus_scheduler_event_generator_stdout.c \
	globus_scheduler_event_generator_stdout.h \
	globus_scheduler_event_generator.h \
	globus_scheduler_event_generator_app.h
globus_scheduler_event_generator_LDFLAGS = -export-dynamic
globus_scheduler_event_generator_LDADD = \
	libglobus_scheduler_event_generator.la \
	$(PACKAGE_DEP_LIBS)

EXTRA_DIST = dirt.sh protocol.dox $(doc_DATA) $(man_MANS) \
        globus-scheduler-event-generator.txt \
        globus-scheduler-event-generator-admin.txt

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_scheduler_event_generator_la_SOURCES)
	rm -rf doc
	$(DOXYGEN)
	rm -f doc/man/man3/globus_scheduler_event_generator.3
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
