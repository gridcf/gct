ACLOCAL_AMFLAGS=-I m4
SUBDIRS = .

bin_PROGRAMS = globusrun
bin_SCRIPTS = \
	globus-job-run \
	globus-job-status \
	globus-job-clean \
	globus-job-get-output \
	globus-job-get-output-helper \
	globus-job-submit \
	globus-job-cancel
man_MANS = globus-job-cancel.1 \
                globus-job-clean.1 \
                globus-job-get-output.1 \
                globus-job-get-output-helper.1 \
                globus-job-run.1  \
                globus-job-status.1 \
                globus-job-submit.1 \
                globusrun.1
doc_DATA = GLOBUS_LICENSE

globusrun_SOURCES = globusrun.c

AM_CPPFLAGS = $(PACKAGE_DEP_CFLAGS)
globusrun_LDADD = $(PACKAGE_DEP_LIBS)


MANUAL_SOURCE = globus-job-cancel.txt \
                globus-job-clean.txt \
                globus-job-get-output.txt \
                globus-job-get-output-helper.txt \
                globus-job-run.txt \
                globus-job-status.txt \
                globus-job-submit.txt \
                globusrun.txt

# EXTRA_DIST is used to include files in
# a source distribution that automake fails to detect.
EXTRA_DIST = globus-job-get-output-helper \
	     $(man_MANS) \
	     $(MANUAL_SOURCE) \
	     $(doc_DATA) \
	     dirt.sh

if BUILD_MANPAGES
SUFFIXES = .txt .1

.txt.1:
	$(A2X) -f manpage -d manpage $<
MAINTAINERCLEANFILES = $(man_MANS)
endif

distuninstallcheck:
	@:
