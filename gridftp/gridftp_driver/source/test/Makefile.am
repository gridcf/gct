AM_CPPFLAGS = $(PACKAGE_DEP_CFLAGS) -I$(top_srcdir)
globus_gridftp_driver_test_LDADD = $(GSI_DRIVER_DLPREOPEN) -dlpreopen ../libglobus_xio_gridftp_driver.la $(PACKAGE_DEP_LIBS) -lltdl

check_SCRIPTS = globus_gridftp_driver_test.pl
check_PROGRAMS = globus_gridftp_driver_test
check_DATA = \
        testcred.key \
        testcred.cert \
        testcred.cakey \
        testcred.cacert \
        testcred.link \
        testcred.signing_policy \
        testcred.srl \
        testcred.gridmap

if ENABLE_TESTS
TESTS = $(check_SCRIPTS)
TESTS_ENVIRONMENT = \
    export X509_USER_CERT=testcred.cert \
    X509_USER_KEY=testcred.key \
    X509_CERT_DIR=$(abs_builddir) \
    GRIDMAP=$(abs_builddir)/testcred.gridmap \
    GRIDFTP_EXE="$(GRIDFTP_EXE)" \
    PATH="$(GRIDFTP_PATH):$(srcdir):$(abs_builddir):$${PATH}";
LOG_COMPILER = $(LIBTOOL) --mode=execute \
	$(GSI_DRIVER_DLOPEN) \
	$(PIPE_DRIVER_DLOPEN) \
	-dlopen $(top_builddir)/libglobus_xio_gridftp_driver.la \
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

EXTRA_DIST = $(check_SCRIPTS) test-wrapper

