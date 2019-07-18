ACLOCAL_AMFLAGS = -I m4

pkgconfdir = $(libdir)/pkgconfig

include_HEADERS = globus_xio_rate_driver.h
lib_LTLIBRARIES = libglobus_xio_rate_driver.la
doc_DATA = GLOBUS_LICENSE
pkgconf_DATA = globus-xio-rate-driver.pc

AM_CPPFLAGS = $(PACKAGE_DEP_CFLAGS)
libglobus_xio_rate_driver_la_LIBADD = $(PACKAGE_DEP_LIBS)
libglobus_xio_rate_driver_la_LDFLAGS = \
        -avoid-version \
        -no-undefined \
        -module \
        -rpath $(libdir)
libglobus_xio_rate_driver_la_SOURCES = globus_xio_rate_driver.c

EXTRA_DIST = dirt.sh $(doc_DATA)

distuninstallcheck:
	@:
