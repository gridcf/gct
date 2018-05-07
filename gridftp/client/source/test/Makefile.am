EXTRA_SCRIPTS = run-tests.pl many-server.pl run-backend-tests.pl test-wrapper

check_SCRIPTS_run = \
	dir-test.pl \
	create-destroy-test.pl \
	exist-test.pl \
	extended-get-test.pl \
	extended-put-test.pl \
	extended-transfer-test.pl \
	lingering-get-test.pl \
	multiple-block-get-test.pl \
	partial-get-test.pl \
	partial-put-test.pl \
	partial-transfer-test.pl \
	plugin-test.pl \
	get-test.pl \
	caching-get-test.pl \
	size-test.pl \
	transfer-test.pl \
	caching-transfer-test.pl \
	caching-extended-get-test.pl \
	user-auth-test.pl
check_SCRIPTS_skip =

if ! TARGET_MINGW
check_SCRIPTS_run += put-test.pl
else
check_SCRIPTS_skip += put-test.pl
endif

if ! TARGET_HURD
check_SCRIPTS_run += bad-buffer-test.pl
else
check_SCRIPTS_skip += bad-buffer-test.pl
endif

check_SCRIPTS = $(check_SCRIPTS_run) $(check_SCRIPTS_skip)

check_DATA_real = \
	FtpTestLib.pm \
	URL.pm
check_DATA = $(check_DATA_real) \
        testcred.key \
        testcred.cert \
        testcred.cakey \
        testcred.cacert \
        testcred.link \
        testcred.signing_policy \
        testcred.srl \
        testcred.gridmap

check_LTLIBRARIES = libglobus_ftp_client_test.la

check_PROGRAMS = \
	ascii-list-test \
	ascii-verbose-list-test \
	ascii-machine-list-test \
	ascii-recursive-list-test \
	bad-buffer-test \
	cache-all-test \
	create-destroy-test \
	cksm-test \
	chmod-test \
	chgrp-test \
	utime-test \
	symlink-test \
	delete-test \
	exist-test \
	extended-get-test \
	extended-get-caching-test \
	extended-put-test \
	extended-transfer-test \
	feat-test \
	lingering-get-test \
	mlst-test \
	mkdir-test \
	modification-time-test \
	move-test \
	multiget-test \
	multiple-bad-buffer-test \
	multiple-block-get-test \
	multiple-block-put-test \
	partial-get-test \
	partial-put-test \
	partial-read-all-test \
	partial-transfer-test \
	plugin-test \
	read-all-test \
	rmdir-test \
	size-test \
	get-test \
	caching-get-test \
	put-test \
	transfer-test \
	caching-transfer-test \
	pipelined-transfer-test \
	user-auth-test \
	restart-marker-test

AM_CPPFLAGS = -I$(top_srcdir) $(OPENSSL_CFLAGS) $(PACKAGE_DEP_CFLAGS)
LDADD = $(GSI_DRIVER_DLPREOPEN) $(PIPE_DRIVER_DLPREOPEN) libglobus_ftp_client_test.la $(top_builddir)/libglobus_ftp_client.la $(OPENSSL_LIBS) $(PACKAGE_DEP_LIBS) -lltdl
AM_LDFLAGS = -dlpreopen force

libglobus_ftp_client_test_la_SOURCES = \
	globus_ftp_client_test_abort_plugin.h \
	globus_ftp_client_test_pause_plugin.h \
	globus_ftp_client_test_restart_plugin.h \
	globus_ftp_client_test_perf_plugin.h \
	globus_ftp_client_test_throughput_plugin.h \
	globus_ftp_client_test_common.h \
	globus_ftp_client_test_abort_plugin.c \
	globus_ftp_client_test_pause_plugin.c \
	globus_ftp_client_test_restart_plugin.c \
	globus_ftp_client_test_perf_plugin.c \
	globus_ftp_client_test_throughput_plugin.c \
	globus_ftp_client_test_common.c
libglobus_ftp_client_test_la_LDFLAGS = $(top_builddir)/libglobus_ftp_client.la $(OPENSSL_LIBS) $(PACKAGE_DEP_LIBS) -rpath $(abs_builddir) -no-undefined

if ENABLE_TESTS
TESTS = $(check_SCRIPTS_run)

TEST_PATH = $(GRIDFTP_SERVER_PATH):$${PATH}
if CYGPATH_W_DEFINED
X509_CERT_DIR = $$($(CYGPATH_W) $(abs_builddir))
GRIDMAP = $$($(CYGPATH_W) $(abs_builddir)/testcred.gridmap)
else
X509_CERT_DIR = $(abs_builddir)
GRIDMAP = $(abs_builddir)/testcred.gridmap
endif
TESTS_ENVIRONMENT = \
    export X509_USER_CERT=testcred.cert \
    X509_USER_KEY=testcred.key \
    X509_CERT_DIR="$(X509_CERT_DIR)" \
    GRIDMAP="$(GRIDMAP)" \
    GRIDFTP_SERVER_EXE="$(GRIDFTP_SERVER_EXE)" \
    PATH="$(TEST_PATH)";
LOG_COMPILER = $(LIBTOOL) --mode=execute \
	$(GSI_DRIVER_DLOPEN) \
	$(PIPE_DRIVER_DLOPEN) \
	$(srcdir)/test-wrapper

# Test CA
.cnf.cacert:
	umask 077; $(OPENSSL) req -passout pass:globus -subj "/CN=ca" -new -x509 -extensions v3_ca -keyout $*.cakey -out $@ -config $<
.cacert.cakey:
	:

.cacert.link:
	linkname="`$(OPENSSL) x509 -hash -noout -in $<`.0"; \
	rm -f "$$linkname"; \
	cp $< "$$linkname"; \
        echo "$$linkname" > $@

.link.signing_policy:
	linkname=`cat $<`; \
	policyfile=$${linkname%.0}.signing_policy; \
	echo "access_id_CA      X509         '/CN=ca'" > $${policyfile}; \
	echo "pos_rights        globus        CA:sign" >> $${policyfile}; \
	echo "cond_subjects     globus       '\"/*\"'" >> $${policyfile}; \
	echo $${policyfile} >> $@

.signing_policy.srl:
	echo 01 > $@

# Test Cert/Key
.srl.key:
	umask 077; $(OPENSSL) genrsa -out $@ 2048
.key.req:
	$(OPENSSL) req -subj "/CN=test" -new -key $< -out $@ -config $*.cnf

.req.cert:
	umask 022; $(OPENSSL) x509 -passin pass:globus -req -days 365 -in testcred.req -CA $*.cacert -CAkey $*.cakey -out $@

.cert.gridmap:
	subject=`$(OPENSSL) x509 -subject -noout -in $< -nameopt rfc2253,-dn_rev | sed -e 's/subject= */\//' -e 's/,/\//g'` ; \
        echo "\"$$subject\" `id -un`" > $@
CLEANFILES = testcred.key testcred.cert testcred.req \
	     testcred.cacert testcred.srl \
	     testcred.cakey testcred.gridmap

clean-local:
	if [ -f testcred.link ]; then \
            rm -f "$$(cat testcred.link)" testcred.link; \
        fi
	if test -f testcred.signing_policy; then \
	    rm -f $$(cat testcred.signing_policy) testcred.signing_policy; \
	fi
SUFFIXES = .key .req .cert .srl .link .signing_policy .cacert .cakey .gridmap
endif

EXTRA_DIST = \
	$(check_DATA_real) \
	$(check_SCRIPTS) \
	$(EXTRA_SCRIPTS)

