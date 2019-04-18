check_PROGRAMS = authz_cred_test
check_LTLIBRARIES = libglobus_authz_test_callout.la
check_DATA = \
        authz_test_callout.conf \
        testcred.key \
        testcred.cert \
        testcred.cakey \
        testcred.cacert \
        testcred.link \
        testcred.signing_policy \
        testcred.srl

AM_CPPFLAGS = $(PACKAGE_DEP_CFLAGS) -I$(top_srcdir)
LDADD = $(PACKAGE_DEP_LIBS) -lltdl

authz_cred_test_LDFLAGS = -dlpreopen libglobus_authz_test_callout.la
authz_cred_test_LDADD = ../libglobus_authz.la $(LDADD)
authz_cred_test_SOURCES = authz_cred_test.c 
authz_cred_test_DEPENDENCIES = libglobus_authz_test_callout.la

libglobus_authz_test_callout_la_SOURCES = authz_test_callout.c
libglobus_authz_test_callout_la_LIBADD = ../libglobus_authz.la $(LDADD)
libglobus_authz_test_callout_la_LDFLAGS = \
	-module \
	-no-undefined \
	-avoid-version \
	-rpath $(abs_builddir)


if ENABLE_TESTS
X509_USER_CERT=testcred.cert
X509_USER_KEY=testcred.key
X509_CERT_DIR=.
if CYGPATH_W_DEFINED
GSI_AUTHZ_CONF=$$($(CYGPATH_W) $(abs_srcdir)/authz_test_callout.conf)
else
GSI_AUTHZ_CONF=$(abs_srcdir)/authz_test_callout.conf
endif

TESTS = $(check_PROGRAMS)
TESTS_ENVIRONMENT = export \
    X509_USER_CERT=$(X509_USER_CERT) \
    X509_USER_KEY=$(X509_USER_KEY) \
    X509_CERT_DIR=$(X509_CERT_DIR) \
    GSI_AUTHZ_CONF=$(GSI_AUTHZ_CONF);
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
SUFFIXES = .key .req .cert .srl .link .signing_policy .cacert .cakey .gridmap
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
endif

EXTRA_DIST = authz_test_callout.conf
