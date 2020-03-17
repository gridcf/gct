globusperldir=$(perlmoduledir)/Globus/GRAM
perldocdir=$(docdir)/perl/Globus/GRAM

MAN_DIR=${mandir}/man3

libexec_SCRIPTS = globus-job-manager-script.pl
sbin_SCRIPTS = globus-gatekeeper-admin
globusperl_DATA = \
    JobDescription.pm \
    JobManager.pm \
    StdioMerger.pm
perldoc_DATA = JobManager.html JobDescription.html
noinst_DATA = JobManager.3pm JobDescription.3pm
doc_DATA = GLOBUS_LICENSE
man_MANS = globus-gatekeeper-admin.8
BUILT_SOURCES = JobDescription.pm

# Files which we want to put in the source package
EXTRA_DIST = \
        dirt.sh \
        $(globusperl_DATA) \
        $(perldoc_DATA) \
        $(noinst_DATA) \
        globus-gatekeeper-admin.txt \
        globus-gatekeeper-admin.8 \
	$(doc_DATA)

SUFFIXES = .pm .html .3pm

.pm.html:
	pod2html --noindex $< > $@

.pm.3pm:
	pod2man --section=3pm $< > $@

install-data-local: $(noinst_DATA)
	$(mkinstalldirs) $(DESTDIR)$(MAN_DIR)
	for manpage in $?; do \
		$(INSTALL_DATA) $$manpage $(DESTDIR)$(MAN_DIR)/Globus::GRAM::`basename $$manpage` || exit 1; \
	done

all-local: $(globusperl_DATA)
	$(MKDIR_P) Globus/GRAM
	if [ -f JobDescription.pm ] ; then \
		$(INSTALL) JobDescription.pm Globus/GRAM ; \
	else \
		$(INSTALL) $(srcdir)/JobDescription.pm Globus/GRAM ; \
	fi
	$(INSTALL) $(srcdir)/JobManager.pm Globus/GRAM
	$(INSTALL) $(srcdir)/StdioMerger.pm Globus/GRAM

clean-local:
	rm -rf Globus

MAINTAINERCLEANFILES = $(perldoc_DATA) $(noinst_DATA) JobDescription.pm

if BUILD_MANPAGES
SUFFIXES += .txt .8

.txt.8:
	$(A2X) -d manpage -f manpage $<
MAINTAINERCLEANFILES += $(man_MANS)
endif

distuninstallcheck:
	@:
