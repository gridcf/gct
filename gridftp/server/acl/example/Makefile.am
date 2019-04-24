lib_LTLIBRARIES = libglobus_gridftp_server_acl_example.la

AM_CPPFLAGS = $(GPT_INCLUDES)
libglobus_gridftp_server_acl_example_la_LIBADD = $(GPT_LIB_LINKS)
libglobus_gridftp_server_acl_example_la_SOURCES = globus_gfs_acl_example.c
libglobus_gridftp_server_acl_example_la_LDFLAGS = \
	$(GPT_LDFLAGS) -module -rpath $(GLOBUS_LOCATION)/lib

EXTRA_DIST = dirt.sh version.h.in
