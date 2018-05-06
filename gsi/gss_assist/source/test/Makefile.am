check_SCRIPTS = \
	gss-assist-impexp-test.pl \
	gss-assist-auth-test.pl \
	gss-assist-gridmap-test.pl \
        gridmap-test.pl \
        gridmap-tools-test.pl \
	read-vhost-cred-dir-test.pl

check_DATA_DIST = grid-mapfile \
       gridmap.blank_line \
       gridmap.empty \
       gridmap.long_line \
       gridmap.multiple-dns \
       gridmap.multiple_lines \
       gridmap.no-local-uid \
       gridmap.no-local-uid2

check_DATA = $(check_DATA_DIST) \
        testcred.key testcred.cert \
        testcred1.cert testcred1.key \
        testcred2.cert testcred2.key \
        testcred.cakey testcred.cacert \
        testcred.link \
        testcred.signing_policy \
        testcred.srl

check_PROGRAMS = \
	gss-assist-auth-init \
	gss-assist-auth-accept \
	gss-assist-impexp-init \
	gss-assist-impexp-accept \
	gss-assist-gridmap \
        gridmap-test \
        gridmap-userok \
	read-vhost-cred-dir-test

AM_CPPFLAGS = -I$(top_srcdir) $(PACKAGE_DEP_CFLAGS)
LDADD = ../libglobus_gss_assist.la $(PACKAGE_DEP_LIBS)

if CYGPATH_W_DEFINED
X509_CERT_DIR=$$($(CYGPATH_W) $(abs_builddir))
TEST_GRIDMAP_DIR=$$($(CYGPATH_W) $(srcdir))
else
X509_CERT_DIR=$(abs_builddir)
TEST_GRIDMAP_DIR=$(srcdir)
endif

if ENABLE_TESTS
TESTS = $(check_SCRIPTS)
TESTS_ENVIRONMENT = export \
    X509_USER_CERT=testcred.cert \
    X509_USER_KEY=testcred.key \
    X509_CERT_DIR=$(X509_CERT_DIR) \
    TEST_GRIDMAP_DIR=$(TEST_GRIDMAP_DIR);

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
testcred.key testcred1.key testcred2.key:
	umask 077; $(OPENSSL) genrsa -out $@ 2048

.key.req:
	$(OPENSSL) req -subj "/CN=$*" -new -key $< -out $@ -config testcred.cnf

.req.cert:
	umask 022; $(OPENSSL) x509 -passin pass:globus -req -days 365 -in $*.req -CA testcred.cacert -CAkey testcred.cakey -out $@

testcred.cert: testcred.srl
testcred1.cert: testcred.cert
testcred2.cert: testcred1.cert

CLEANFILES = \
	testcred.key testcred.cert testcred.req \
	testcred1.key testcred1.cert testcred1.req \
	testcred2.key testcred2.cert testcred2.req \
	testcred.cacert testcred.srl \
	testcred.cakey \
	exported_accept_context exported_init_context \
	gridmap.script-test gridmap.script-test.old

clean-local:
	if [ -f testcred.link ]; then \
            rm -f "$$(cat testcred.link)" testcred.link; \
        fi
	if test -f testcred.signing_policy; then \
	    rm -f $$(cat testcred.signing_policy) testcred.signing_policy; \
	fi
SUFFIXES = .key .req .cert .srl .link .signing_policy .cacert .cakey
endif

gss_assist_auth_init_SOURCES = gss_assist_auth_init.c tokens_bsd.c tokens_bsd.h
gss_assist_auth_accept_SOURCES = gss_assist_auth_accept.c tokens_bsd.c tokens_bsd.h
gss_assist_impexp_init_SOURCES = gss_assist_impexp_init.c tokens_bsd.c tokens_bsd.h
gss_assist_impexp_accept_SOURCES = gss_assist_impexp_accept.c tokens_bsd.c tokens_bsd.h
gss_assist_gridmap_SOURCES = gss_assist_gridmap.c
gridmap_test_SOURCES = gridmap_test.c
gridmap_userok_SOURCES = gridmap_userok.c
read_vhost_cred_dir_test_SOURCES = read_vhost_cred_dir_test.c

EXTRA_DIST = \
        $(check_DATA_DIST) \
        $(check_SCRIPTS)
