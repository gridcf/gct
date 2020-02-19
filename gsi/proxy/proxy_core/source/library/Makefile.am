include_HEADERS = \
	globus_gsi_proxy.h \
	globus_gsi_proxy_constants.h 
lib_LTLIBRARIES = libglobus_gsi_proxy_core.la

libglobus_gsi_proxy_core_la_CPPFLAGS = \
	-I$(top_builddir) -I$(top_srcdir)/library \
	$(PACKAGE_DEP_CFLAGS) $(OPENSSL_CFLAGS)
libglobus_gsi_proxy_core_la_LIBADD = $(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS)
libglobus_gsi_proxy_core_la_LDFLAGS = \
	-version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined
if USE_SYMBOL_VERSIONS
libglobus_gsi_proxy_core_la_LDFLAGS += -Wl,--version-script,$(srcdir)/sym
endif

libglobus_gsi_proxy_core_la_SOURCES = \
	globus_gsi_proxy.h \
	globus_gsi_proxy.c \
	globus_i_gsi_proxy.h \
	globus_gsi_proxy_handle.c \
	globus_gsi_proxy_handle_attrs.c \
	globus_gsi_proxy_constants.h \
	globus_gsi_proxy_error.c 

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_gsi_proxy_core_la_SOURCES)
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
EXTRA_DIST=sym
