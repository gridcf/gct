if ENABLE_TESTS
check_DATA = \
        testcred.key \
        testcred.cert \
        testcred.cakey \
        testcred.cacert \
        testcred.link \
        testcred.signing_policy \
        testcred.srl
endif

check_PROGRAMS = activate-test callback-contact-test

AM_CPPFLAGS = $(PACKAGE_DEP_CFLAGS) -I$(top_srcdir)
LDADD =  $(GLOBUS_XIO_GSI_DLPREOPEN) ../libglobus_gram_client.la $(PACKAGE_DEP_LIBS) -lltdl
AM_LDFLAGS = -dlpreopen force


if ENABLE_TESTS
TESTS = $(check_PROGRAMS)

X509_USER_CERT = testcred.cert
X509_USER_KEY = testcred.key
if USE_CYGPATH_W
X509_CERT_DIR = $$($(CYGPATH_W) $(abs_builddir))
else
X509_CERT_DIR = $(abs_builddir)
endif

TESTS_ENVIRONMENT = export \
    X509_USER_CERT=$(X509_USER_CERT) \
    X509_USER_KEY=$(X509_USER_KEY) \
    X509_CERT_DIR=$(X509_CERT_DIR);
LOG_COMPILER = $(LIBTOOL) --mode=execute $(GLOBUS_XIO_GSI_DLOPEN)

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

CLEANFILES = testcred.key testcred.cert testcred.req \
	     testcred.cacert testcred.srl \
	     testcred.cakey

clean-local:
	@if test -r testcred.link ; then \
            rm -f $$(cat testcred.link) testcred.link ; \
        fi
	if test -f testcred.signing_policy; then \
	    rm -f $$(cat testcred.signing_policy) testcred.signing_policy; \
	fi
SUFFIXES = .key .req .cert .srl .link .signing_policy .cacert .cakey
endif
