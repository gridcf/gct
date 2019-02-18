ACLOCAL_AMFLAGS = -I m4

SUBDIRS = rvf scripts seg . test

validationdir = $(datadir)/globus/$(PACKAGE)
globusconfigdir = ${sysconfdir}/globus
logrotatedir = ${sysconfdir}/logrotate.d

globusconfig_DATA = globus-gram-job-manager.conf
noinst_LTLIBRARIES = libglobus_gram_job_manager.la
sbin_PROGRAMS = \
        globus-job-manager \
        globus-gram-streamer \
        globus-job-manager-lock-test
bin_SCRIPTS = globus-personal-gatekeeper
validation_DATA = globus-gram-job-manager.rvf
man_MANS = globus-personal-gatekeeper.1 rsl.5 globus-job-manager.8
doc_DATA = GLOBUS_LICENSE

libglobus_gram_job_manager_la_LIBADD = \
	$(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS)
libglobus_gram_job_manager_la_SOURCES = \
	globus_gram_job_manager.h \
        globus_gram_job_manager_config.c \
        globus_gram_job_manager.c \
	globus_gram_job_manager_auditing.c \
	globus_gram_job_manager_contact.c \
	globus_gram_job_manager_gsi.c \
	globus_gram_job_manager_history_file.c \
	globus_gram_job_manager_query.c \
	globus_gram_job_manager_request.c \
	globus_gram_job_manager_rsl.c \
	globus_gram_job_manager_script.c \
	globus_gram_job_manager_seg.c \
	globus_gram_job_manager_staging.c \
	globus_gram_job_manager_state.c \
	globus_gram_job_manager_state_file.c \
	globus_gram_job_manager_validate.c \
        logging.c \
        startup_socket.c \
        tg_gateway.c
nodist_libglobus_gram_job_manager_la_SOURCES = \
        default_rvf.c

globus_job_manager_SOURCES = main.c
globus_job_manager_lock_test_SOURCES = lock_test.c

default_rvf.c: $(srcdir)/globus-gram-job-manager.rvf
	(echo "char globus_i_gram_default_rvf[] = \"\\"; \
         sed -e 's/\\/\\\\/g' \
             -e 's/"/\\"/g' \
             -e 's/$$/\\n\\/' \
            < $(srcdir)/globus-gram-job-manager.rvf; \
         echo "\";") > default_rvf.c

# Compiler flags
AM_CPPFLAGS = $(PACKAGE_DEP_CFLAGS) \
              $(OPENSSL_CFLAGS) \
              -I$(top_srcdir) \
              -I$(top_builddir) \
              -I$(srcdir)/rvf \
              $(XML_CPPFLAGS)
globus_job_manager_LDADD = \
	./libglobus_gram_job_manager.la \
	./rvf/libglobus_rvf.la \
	$(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS) $(XML_LIBS)

globus_gram_streamer_SOURCES = globus_gram_streamer.c
globus_gram_streamer_LDADD = \
	./libglobus_gram_job_manager.la \
	./rvf/libglobus_rvf.la \
	$(PACKAGE_DEP_LIBS) $(OPENSSL_LIBS) $(XML_LIBS)


MANUAL_SOURCE = globus-personal-gatekeeper.txt \
                globus-job-manager.txt \
                rsl.txt


# Files which we want to put in the source package
EXTRA_DIST = dirt.sh \
	     $(validation_DATA) \
	     configuration.dox \
	     globus_gram_job_manager_script_interface.dox \
	     globus_gram_job_manager_script_tutorial.dox \
	     globus_job_manager_rsl_validation.dox \
	     job_execution_environment.dox \
             $(MANUAL_SOURCE) \
             $(man_MANS) \
	     $(doc_DATA) \
	     main.dox \
	     dirt.sh

globus_gram_job_manager_rsl.dox: globus-gram-job-manager.rvf
	perl $(top_srcdir)/scripts/create_rsl_documentation.pl < $< > $@
rsl.txt: globus-gram-job-manager.rvf
	perl $(top_srcdir)/scripts/create_rsl_documentation.pl -a < $< > $@

install-data-hook:
	mkdir -p $(DESTDIR)$(localstatedir)/lib/globus/gram_job_state
	chmod 1777 $(DESTDIR)$(localstatedir)/lib/globus/gram_job_state
	mkdir -p $(DESTDIR)$(logrotatedir)
	cp $(builddir)/gram.logrotate $(DESTDIR)$(logrotatedir)/globus-job-manager
	mkdir -p $(DESTDIR)@LOGDIR@
	chmod 1777 $(DESTDIR)@LOGDIR@
	mkdir -p $(DESTDIR)$(sysconfdir)/globus/gram

CLEANFILES = \
    default_rvf.c \
    globus_gram_job_manager_rsl.dox

if BUILD_MANPAGES
SUFFIXES = .txt .1 .5 .8

.txt.1 .txt.5 .txt.8:
	$(A2X) -d manpage -f manpage $<

MAINTAINERCLEANFILES = $(man_MANS)
endif

distuninstallcheck:
	@:
