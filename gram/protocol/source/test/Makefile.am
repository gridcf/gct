AM_CPPFLAGS = -I$(top_srcdir) -I$(top_builddir) $(PACKAGE_DEP_CFLAGS)
LDADD = $(GSI_DRIVER_LIBS) $(DLPREOPEN_FORCE) \
    ../libglobus_gram_protocol.la $(PACKAGE_DEP_LIBS) -lltdl

check_SCRIPTS = gram-protocol-test-wrapper 

check_PROGRAMS = \
	allow-attach-test \
	delegation-test \
	io-test \
	pack-test \
        create-extensions-test \
        error-test \
        pack-with-extensions-test \
        unpack-job-request-reply-with-extensions-test \
        unpack-message-test \
        unpack-status-reply-with-extensions-test \
        unpack-with-extensions-test

check_DATA = \
        testcred.key \
        testcred.cert \
        testcred.cakey \
        testcred.cacert \
        testcred.link \
        testcred.signing_policy \
        testcred.srl

if ENABLE_TESTS
TESTS = $(check_PROGRAMS)

if CYGPATH_W_DEFINED
X509_CERT_DIR = $$($(CYGPATH_W) "$(abs_builddir)")
else
X509_CERT_DIR = $(abs_builddir)
endif

TESTS_ENVIRONMENT = export \
    X509_USER_CERT=testcred.cert \
    X509_USER_KEY=testcred.key \
    X509_CERT_DIR=$(X509_CERT_DIR);

LOG_COMPILER = $(LIBTOOL) --mode=execute $(GSI_DRIVER_LIBS) \
    $(srcdir)/gram-protocol-test-wrapper

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
	if [ -f testcred.link ]; then \
            rm -f "$$(cat testcred.link)" testcred.link; \
        fi
	if test -f testcred.signing_policy; then \
	    rm -f $$(cat testcred.signing_policy) testcred.signing_policy; \
	fi
	rm -f *.valgrind *.vxml

SUFFIXES = .key .req .cert .srl .link .signing_policy .cacert .cakey .gridmap
endif

EXTRA_DIST = $(check_SCRIPTS) 
