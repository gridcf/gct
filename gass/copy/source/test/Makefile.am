EXTRA_SCRIPTS = test-wrapper

check_SCRIPTS_skip =
check_SCRIPTS_run = \
        guc-fw.pl \
        guc-pp-cc.pl \
        guc-stack.pl
if MINGW32
check_SCRIPTS_skip += guc-cc.pl
else
check_SCRIPTS_run += guc-cc.pl
endif

check_SCRIPTS = $(check_SCRIPTS_skip) $(check_SCRIPTS_run)

if ENABLE_TESTS
check_DATA =  \
        testcred.key \
        testcred.cert \
        testcred.cakey \
        testcred.cacert \
        testcred.link \
        testcred.signing_policy \
        testcred.srl \
        testcred.gridmap

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

if CYGPATH_W_DEFINED
X509_CERT_DIR = $$($(CYGPATH_W) $(abs_builddir))
GRIDMAP = $$($(CYGPATH_W) $(abs_builddir)/testcred.gridmap)
CYGPATH_W_DEFINED=CYGPATH_W_DEFINED=1
else
X509_CERT_DIR = $(abs_builddir)
GRIDMAP = $(abs_builddir)/testcred.gridmap
CYGPATH_W_DEFINED=
endif

TEST_PATH = $(GRIDFTP_SERVER_PATH):$(abs_top_builddir):$${PATH}
TESTS = $(check_SCRIPTS_run)
TESTS_ENVIRONMENT = \
    export X509_USER_CERT=testcred.cert \
    X509_USER_KEY=testcred.key \
    GRIDFTP_SERVER_EXE=$(GRIDFTP_SERVER_EXE) \
    X509_CERT_DIR=$(X509_CERT_DIR) \
    GRIDMAP=$(GRIDMAP) \
    PATH="$(TEST_PATH)" \
    $(CYGPATH_W_DEFINED);

LOG_COMPILER = $(LIBTOOL) --mode=execute \
    ${GSI_DRIVER_LIBS} \
    ${PIPE_DRIVER_LIBS} \
    $(srcdir)/test-wrapper

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
EXTRA_DIST = $(check_SCRIPTS) $(EXTRA_SCRIPTS)
