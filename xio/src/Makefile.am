ACLOCAL_AMFLAGS=-I m4
pkgconfigdir = $(libdir)/pkgconfig
SUBDIRS= builtins . test

AM_CPPFLAGS = -DGLOBUS_BUILTIN=1

include_HEADERS = \
    globus_xio_types.h                          \
    globus_xio_util.h                           \
    globus_xio_load.h                           \
    globus_xio_driver.h                         \
    globus_xio.h                                \
    globus_xio_wrapblock.h                      \
    globus_xio_system.h 

noinst_HEADERS = \
    globus_i_xio.h \
    globus_i_xio_system_common.h

lib_LTLIBRARIES = libglobus_xio.la
pkgconfig_DATA = globus-xio.pc


libglobus_xio_la_SOURCES =                          \
        globus_xio_handle.c                         \
        globus_xio_driver.c                         \
        globus_xio_attr.c                           \
        globus_xio_timeout.c                        \
        globus_xio_server.c                         \
        globus_xio_pass.c                           \
        globus_xio_util.c                           \
        globus_xio_wrapblock.c                      \
        globus_xio_system_select.c                  \
        globus_xio_system_win32.c                   \
        globus_i_xio_win32_complete.c               \
        globus_i_xio_win32_file.c                   \
        globus_i_xio_win32_mode.c                   \
        globus_i_xio_win32_socket.c                 \
        globus_i_xio_system_common.c                \
        globus_xio_load.c
libglobus_xio_la_CPPFLAGS = $(AM_CPPFLAGS) $(PACKAGE_DEP_CFLAGS)
libglobus_xio_la_LDFLAGS = \
    -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
    -no-undefined
libglobus_xio_la_LIBADD = $(PACKAGE_DEP_LIBS) $(XIO_BUILTIN_LIBS)

doc_DATA = GLOBUS_LICENSE

EXTRA_DIST = \
        globus-vararg-enums-doxygen-filter.pl \
        globus_xio_system_win32.c \
        globus_i_xio_win32.h \
        globus_i_xio_win32_complete.c \
        globus_i_xio_win32_file.c \
        globus_i_xio_win32_mode.c \
        globus_i_xio_win32_socket.c \
        dirt.sh \
        $(doc_DATA)

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_xio_la_SOURCES)
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
