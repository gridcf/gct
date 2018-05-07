check_PROGRAMS = \
	globus_io_tcp_test \
	globus_io_tcp_test_server \
	globus_io_tcp_test_client \
	globus_io_file_test \
	globus_io_authorization_test

check_SCRIPTS = \
	globus-io-authorization-test.pl \
        globus-io-file-test.pl \
        globus-io-tcp-test.pl

check_DATA = \
        testcred.key \
        testcred.cert \
        testcred.cakey \
        testcred.cacert \
        testcred.link \
        testcred.signing_policy \
        testcred.srl

AM_CPPFLAGS = -I$(top_srcdir) $(PACKAGE_DEP_CFLAGS) $(GSI_DRIVER_CFLAGS)
LDADD = ../libglobus_io.la $(PACKAGE_DEP_LIBS) $(GSI_DRIVER_DLPREOPEN) -lltdl
AM_LDFLAGS = -dlpreopen force

if CYGPATH_W_DEFINED
X509_CERT_DIR = $$($(CYGPATH_W) $(abs_builddir))
else
X509_CERT_DIR = $(abs_builddir)
endif

if ENABLE_TESTS
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

TESTS = $(check_SCRIPTS)
TESTS_ENVIRONMENT = export \
    X509_USER_CERT=testcred.cert \
    X509_USER_KEY=testcred.key \
    X509_CERT_DIR=$(X509_CERT_DIR);

LOG_COMPILER = $(srcdir)/globus-io-test-wrapper
SUFFIXES = .key .req .cert .srl .link .signing_policy .cacert .cakey
CLEANFILES = testcred.cert testcred.req testcred.key testcred.cacert testcred.cakey testcred.srl

clean-local:
	if test -f testcred.link; then \
	    rm -f $$(cat testcred.link) testcred.link; \
	fi
	if test -f testcred.signing_policy; then \
	    rm -f $$(cat testcred.signing_policy) testcred.signing_policy; \
	fi
endif

EXTRA_DIST = \
	random_data.txt \
	$(check_SCRIPTS) \
        globus-io-test-wrapper
