include_HEADERS = globus_xio_skeleton_driver.h
lib_LTLIBRARIES = libglobus_xio_skeleton_driver.la

AM_CPPFLAGS = $(GPT_INCLUDES)
libglobus_xio_skeleton_driver_la_LIBADD = $(GPT_LIB_LINKS)
libglobus_xio_skeleton_driver_la_SOURCES = globus_xio_skeleton_driver.c
libglobus_xio_skeleton_driver_la_LDFLAGS = \
	$(GPT_LDFLAGS) -module -rpath $(GLOBUS_LOCATION)/lib

EXTRA_DIST = dirt.sh version.h.in
