include_HEADERS = globus_xio_token_bucket_driver.h
lib_LTLIBRARIES = libglobus_xio_token_bucket_driver.la

AM_CPPFLAGS = $(GPT_INCLUDES)
libglobus_xio_token_bucket_driver_la_LIBADD = $(GPT_LIB_LINKS)
libglobus_xio_token_bucket_driver_la_SOURCES = globus_xio_token_bucket_driver.c
libglobus_xio_token_bucket_driver_la_LDFLAGS = \
	$(GPT_LDFLAGS) -module -rpath $(libdir)

EXTRA_DIST = dirt.sh version.h.in
