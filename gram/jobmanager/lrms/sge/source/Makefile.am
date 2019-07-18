ACLOCAL_AMFLAGS = -I m4

SUBDIRS = seg

globussysconfdir = ${sysconfdir}/globus
gridservicesdir = ${sysconfdir}/grid-services/available
gramperlmoduledir = ${perlmoduledir}/Globus/GRAM/JobManager
validationdir = $(datadir)/globus/globus_gram_job_manager

globussysconf_DATA = globus-sge.conf
gridservices_DATA = jobmanager-sge-poll jobmanager-sge-seg
gramperlmodule_DATA = sge.pm
validation_DATA = sge.rvf
doc_DATA = AUTHORS CREDITS LICENSE GLOBUS_LICENSE LICENSE.LGPL

AT=@
jobmanager-sge-poll: jobmanager-sge-poll.in
	sed -e "s|$(AT)sbindir$(AT)|$(sbindir)|" \
	    -e "s|$(AT)sysconfdir$(AT)|$(sysconfdir)|" < $< > $@

jobmanager-sge-seg: jobmanager-sge-seg.in
	sed -e "s|$(AT)sbindir$(AT)|$(sbindir)|" \
	    -e "s|$(AT)sysconfdir$(AT)|$(sysconfdir)|" < $< > $@



EXTRA_DIST = \
        dirt.sh \
        sge.pm \
        $(doc_DATA) \
        $(validation_DATA) \
        jobmanager-sge-poll.in \
        jobmanager-sge-seg.in

CLEANFILES = jobmanager-sge-poll jobmanager-sge-seg

# Install link to available file after all other install happens
# native packages can remove this and use globus-gatekeeper-admin to deal
# with the link
if SEG_ENABLED_COND
install-data-hook:
	cd $(DESTDIR)$(gridservicesdir)/..; \
	[ -L "jobmanager-sge" ] || \
	ln -s available/jobmanager-sge-seg jobmanager-sge
else
install-data-hook:
	cd $(DESTDIR)$(gridservicesdir)/..; \
	[ -L "jobmanager-sge" ] || \
	ln -s available/jobmanager-sge-poll jobmanager-sge
endif

distuninstallcheck:
	@:
