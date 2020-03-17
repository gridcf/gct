# Extra directories into which we install files
globusperldir=$(perlmoduledir)/Globus/GRAM
perldocdir=$(docdir)/perl/Globus/GRAM

# Not handled by automake, because of :: in the destination file names
MAN_DIR=${mandir}/man3

# What gets installed where
globusperl_DATA = Error.pm JobSignal.pm JobState.pm
perldoc_DATA = Error.html JobSignal.html JobState.html
noinst_DATA = Error.3pm JobSignal.3pm JobState.3pm

BUILT_SOURCES = $(globusperl_DATA)

EXTRA_DIST = $(globusperl_DATA) $(perldoc_DATA) $(noinst_DATA) \
	     create_protocol_constants.pl

SUFFIXES = .pm .html .3pm

$(globusperl_DATA): create_protocol_constants.pl ../globus-gram-protocol-constants.sh
	$(RM) $(globusperl_DATA)
	-$(PERL) $(srcdir)/create_protocol_constants.pl -perl \
		$(srcdir)/../globus-gram-protocol-constants.sh

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
	for mod in Error.pm JobSignal.pm JobState.pm; do \
            if [ ! -f "$$mod" ] && [ -f "$(srcdir)/$$mod" ]; then \
                mod="$(srcdir)/$$mod"; \
            fi; \
            $(INSTALL) "$$mod" Globus/GRAM ; \
        done;

clean-local:
	rm -rf Globus

MAINTAINERCLEANFILES = $(perldoc_DATA) $(noinst_DATA) $(globusperl_DATA)
