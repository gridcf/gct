SUBDIRS = records jobs perl_modules

pkgtestdir = $(GLOBUS_LOCATION)/test/$(PACKAGE)

Scripts = \
	TESTS.pl \
	globus-gram-job-manager-auditing-upload-success-test.pl \
	globus-gram-job-manager-auditing-upload-failure-test.pl

GeneratedScripts = \
	globus-gram-job-manager-auditing-tg-job-submission-test.pl \
	globus-gram-job-manager-auditing-job-submission-test.pl

GeneratedScriptsSources = \
	globus-gram-job-manager-auditing-tg-job-submission-test.pl.in \
	globus-gram-job-manager-auditing-job-submission-test.pl.in

pkgtest_SCRIPTS = $(Scripts) $(GeneratedScripts)

pkgtest_DATA = \
	testcert.pem \
	testkey.pem \
	b38b4d8c.0 \
	b38b4d8c.signing_policy \
	bootstrap \
	testproxy.pem \
	shibproxy.pem

EXTRA_DIST = dirt.sh $(Scripts) $(GeneratedScriptsSources) $(pkgtest_DATA)
