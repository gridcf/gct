check_PROGRAMS = \
		handle-attrs-test \
		proxy-core-test \
		proxy-handle-compat-test \
		proxy-handle-test

AM_CPPFLAGS = -I$(top_srcdir)/library -I.. $(PACKAGE_DEP_CFLAGS) $(OPENSSL_CFLAGS)
LDADD = ../library/libglobus_gsi_proxy_core.la $(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS)

TESTS = \
	handle-attrs-test \
	proxy-core-test \
	proxy-handle-compat-test \
	proxy-handle-test

LOG_COMPILER = $(LIBTOOL)
AM_LOG_FLAGS = --mode=execute $(VALGRIND)
