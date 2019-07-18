ACLOCAL_AMFLAGS = -I m4

DIST_SUBDIRS = config init

if BUILD_GATEKEEPER
SUBDIRS = $(DIST_SUBDIRS)

logdir = ${localstatedir}/log
logrotatedir = ${sysconfdir}/logrotate.d

sbin_PROGRAMS = \
	globus-gatekeeper \
	globus-k5
man_MANS = globus-gatekeeper.8 globus-k5.8
doc_DATA = GLOBUS_LICENSE

AM_CPPFLAGS = $(PACKAGE_DEP_CFLAGS) $(OPENSSL_CFLAGS)

globus_gatekeeper_LDADD = $(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS)
globus_gatekeeper_CPPFLAGS = \
    $(AM_CPPFLAGS) \
    -DGLOBUS_LOCATION=\"$(prefix)\" -DGLOBUS_LOG_DIR=\"$(localstatedir)\"
globus_gatekeeper_SOURCES = \
	globus_gatekeeper.c \
	globus_gatekeeper_utils.h \
	globus_gatekeeper_utils.c

globus_k5_LDADD = $(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS)
globus_k5_CPPFLAGS = $(AM_CPPFLAGS)
globus_k5_SOURCES = globus_k5.c globus_gatekeeper_utils.c

install-data-hook: gatekeeper.logrotate
	mkdir -p $(DESTDIR)$(logdir)
	mkdir -p $(DESTDIR)$(logrotatedir)
	cp gatekeeper.logrotate $(DESTDIR)$(logrotatedir)/globus-gatekeeper

SUFFIXES = .logrotatein .logrotate
.logrotatein.logrotate:
	sed -e 's|@LOGDIR@|${localstatedir}/log|' < $< > $@

CLEANFILES = gatekeeper.logrotate

EXTRA_DIST=$(doc_DATA) $(man_MANS) \
        globus-gatekeeper.txt \
        globus-k5.txt \
        gatekeeper.logrotatein

if BUILD_MANPAGES
SUFFIXES += .txt .8

.txt.8:
	$(A2X) -d manpage -f manpage $<

MAINTAINERCLEANFILES = $(man_MANS)
endif
endif

distuninstallcheck:
	@:
