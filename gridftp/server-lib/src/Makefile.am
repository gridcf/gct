ACLOCAL_AMFLAGS = -I m4

SUBDIRS = . test

pkgconfigdir = $(libdir)/pkgconfig

include_HEADERS = globus_gridftp_server_control.h globus_xio_gssapi_ftp.h
lib_LTLIBRARIES = libglobus_gridftp_server_control.la
doc_DATA = GLOBUS_LICENSE
pkgconfig_DATA = globus-gridftp-server-control.pc

libglobus_gridftp_server_control_la_CPPFLAGS = $(PACKAGE_DEP_CFLAGS)
libglobus_gridftp_server_control_la_LIBADD = $(PACKAGE_DEP_LIBS)
libglobus_gridftp_server_control_la_LDFLAGS = \
    -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
    -no-undefined
libglobus_gridftp_server_control_la_SOURCES = \
    globus_i_gridftp_server_control.h                   \
    globus_gridftp_server_control.c                     \
    globus_gridftp_server_control_attr.c                \
    globus_gridftp_server_control_accessors.c           \
    globus_gridftp_server_control_events.c              \
    globus_gridftp_server_control_commands.c            \
    globus_xio_gssapi_ftp.c

EXTRA_DIST = dirt.sh $(doc_DATA)

distuninstallcheck:
	@:
