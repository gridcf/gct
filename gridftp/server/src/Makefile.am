ACLOCAL_AMFLAGS=-I m4
SUBDIRS = modules init . test

pkgconfigdir = $(libdir)/pkgconfig
gridftpconfdir = ${sysconfdir}

doc_DATA = GLOBUS_LICENSE
man_MANS = globus-gridftp-password.8 globus-gridftp-server-setup-chroot.8 \
	globus-gridftp-server.8
gridftpconf_DATA = \
        gridftp.conf.default gridftp.xinetd.default gridftp.gfork.default 
include_HEADERS = globus_gridftp_server.h globus_gridftp_server_embed.h
lib_LTLIBRARIES = libglobus_gridftp_server.la
noinst_PROGRAMS = globus-gridftp-server-noinst
sbin_PROGRAMS= \
        globus-gridftp-server \
        gfs-gfork-master \
        gfs-dynbe-client
sbin_SCRIPTS = \
	globus-gridftp-password \
	globus-gridftp-server-enable-sshftp \
        globus-gridftp-server-setup-chroot
pkgconfig_DATA = globus-gridftp-server.pc

AM_CPPFLAGS = $(PACKAGE_DEP_CFLAGS) $(OPENSSL_CFLAGS) -DGLOBUS_BUILTIN=1
AM_LDFLAGS = -dlpreopen force
libglobus_gridftp_server_la_LIBADD = $(MODULE_LIBS) $(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS)
libglobus_gridftp_server_la_LDFLAGS =   \
        -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined
libglobus_gridftp_server_la_SOURCES =   \
	globus_gridftp_server.h         \
	globus_gridftp_server_embed.h   \
	globus_i_gridftp_server.h       \
        globus_i_gfs_ftp_response_error.c   \
	globus_i_gfs_log.c              \
	globus_i_gfs_brain.c            \
	globus_i_gfs_default_brain.c    \
	globus_i_gfs_log.h              \
	globus_i_gfs_config.c           \
	globus_i_gfs_config.h           \
	globus_i_gfs_acl_test.c         \
	globus_i_gfs_acl.c              \
	globus_i_gfs_acl.h              \
	globus_i_gfs_data.c             \
	globus_i_gfs_data.h             \
	globus_i_gfs_cas.c              \
	globus_i_gfs_embed.c            \
	globus_i_gfs_ipc.c              \
	globus_i_gfs_ipc.h              \
	globus_i_gfs_control.c          \
	gfs_i_gfork_plugin.h            \
	globus_i_gfs_control.h

globus_gridftp_server_SOURCES = globus_gridftp_server.c
globus_gridftp_server_LDADD = libglobus_gridftp_server.la $(MODULE_DLOPEN) $(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS) -lltdl

globus_gridftp_server_noinst_SOURCES = globus_gridftp_server.c
globus_gridftp_server_noinst_LDADD = libglobus_gridftp_server.la $(MODULE_DLPREOPEN) $(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS) -lltdl

gfs_gfork_master_SOURCES = gfs_gfork_master.c
gfs_gfork_master_LDADD = $(preload_links) $(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS) -lltdl

gfs_dynbe_client_SOURCES = gfs_dynbe_client.c
gfs_dynbe_client_LDADD = $(preload_links) $(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS) -lltdl

if LINK_WITH_CXX
globus_gridftp_server_LINK = $(CXXLINK)
gfs_gfork_master_LINK = $(CXXLINK)
gfs_dynbe_client_LINK = $(CXXLINK)
else
globus_gridftp_server_LINK = $(LINK)
gfs_gfork_master_LINK = $(LINK)
gfs_dynbe_client_LINK = $(LINK)
endif

# Dummy targets to cause automake to include CXXLINK definition
nodist_EXTRA_unused_SOURCES = unused.cxx
unused_SOURCES = 
EXTRA_PROGRAMS = unused

if BUILD_MANPAGES
SUFFIXES = .txt .8

.txt.8:
	$(A2X) -d manpage -f manpage $<

MAINTAINERCLEANFILES = $(man_MANS)
endif

EXTRA_DIST = \
    dirt.sh \
    dsi_bones \
    globus-gridftp-server-setup-chroot \
    $(doc_DATA) \
    $(man_MANS) \
    globus-gridftp-password.txt \
    globus-gridftp-server-setup-chroot.txt \
    globus-gridftp-server.txt \
    globus-gridftp-server-options.txt

distuninstallcheck:
	@:
