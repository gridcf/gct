ACLOCAL_AMFLAGS = -I m4

pkgconfigdir = $(libdir)/pkgconfig

include_HEADERS = globus_xio_pipe_driver.h
lib_LTLIBRARIES = libglobus_xio_pipe_driver.la
doc_DATA = GLOBUS_LICENSE
pkgconfig_DATA = globus-xio-pipe-driver.pc

AM_CPPFLAGS = $(PACKAGE_DEP_CFLAGS)
libglobus_xio_pipe_driver_la_LIBADD = $(PACKAGE_DEP_LIBS)
libglobus_xio_pipe_driver_la_SOURCES = globus_xio_pipe_driver.c
libglobus_xio_pipe_driver_la_LDFLAGS = \
        -no-undefined \
        -avoid-version \
        -module -rpath $(libdir)

EXTRA_DIST = dirt.sh $(doc_DATA)

distuninstallcheck:
	@:
