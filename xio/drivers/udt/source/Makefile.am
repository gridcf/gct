ACLOCAL_AMFLAGS = -I m4

SUBDIRS = $(LIBFFI_SUBDIR) $(GETTEXT_SUBDIR) $(GLIB2_SUBDIR) $(LIBNICE_SUBDIR) $(UDT_SUBDIR) .
DIST_SUBDIRS = libffi gettext glib2 libnice udt .

pkgconfigdir = $(libdir)/pkgconfig
include_HEADERS = globus_xio_udt_ref.h
doc_DATA = GLOBUS_LICENSE
lib_LTLIBRARIES = libglobus_xio_udt_driver.la
pkgconfig_DATA = globus-xio-udt-driver.pc

AM_CPPFLAGS = $(PACKAGE_DEP_CFLAGS) $(UDT_CPPFLAGS) $(NICE_CFLAGS) $(GLIB2_CFLAGS)

libglobus_xio_udt_driver_la_LIBADD = \
        $(PACKAGE_DEP_LIBS) $(UDT_LDFLAGS) $(NICE_LIBS) $(GLIB2_LIBS) $(LIBFFI_LIBS) -lstdc++
libglobus_xio_udt_driver_la_SOURCES = \
        globus_xio_udt_ref.cpp  \
        globus_xio_udt_ref.h  \
	ice.c ice.h
if HOST_OSX
link_frameworks = -framework CoreFoundation -framework Cocoa
endif

libglobus_xio_udt_driver_la_LDFLAGS = \
        -module -no-undefined -avoid-version -rpath $(libdir) \
        $(link_frameworks)

EXTRA_DIST = dirt.sh $(doc_DATA) empty.xz
