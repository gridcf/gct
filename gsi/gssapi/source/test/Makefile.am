AM_CPPFLAGS = -I$(top_srcdir)/library $(PACKAGE_DEP_CFLAGS) $(OPENSSL_CFLAGS)
LDADD = libgssapi_test_utils.la \
        ../library/libglobus_gssapi_gsi.la \
        $(PACKAGE_DEP_LIBS) \
        $(OPENSSL_LIBS) \
        -lltdl

check_LTLIBRARIES = libgssapi_test_utils.la

check_DATA_real = \
        compare_name_test_hybrid.txt \
        compare_name_test_strict_gt2.txt \
        compare_name_test_strict_rfc2818.txt \
        test.example.org.pem \
        star.example.org.pem \
        192.168.1.1.example.org.pem \
        192.168.1.1-2.example.org.pem

testcred_links = testcred.link
signingpolicy_links = testcred.signing_policy

test_creds = \
        testcred.key \
        testcred.cert \
        testcred-dns1.key \
        testcred-dns1.cert \
        testcred-dns2.key \
        testcred-dns2.cert

check_DATA = $(check_DATA_real) \
        testcred.cakey \
        testcred.cacert \
        testcred.srl \
        $(test_creds) \
        $(testcred_links) \
	$(signingpolicy_links) \
        testcred-nocn.key \
        testcred-nocn.cert \
        copy_cases \
        vhost_setup \
        $(THREAD_TESTS)

check_SCRIPTS = \
        compare-name-test-hybrid.pl \
        compare-name-test-gt2.pl \
        compare-name-test-rfc2818.pl \
        gssapi-thread-test-wrapper \
        import-cred-test.pl \
	mech-compatibility-test.pl

check_PROGRAMS= \
	alpn-test \
        compare-name-test \
        duplicate-name-test \
	error-test \
	gssapi-anonymous-test \
	gssapi-delegation-test \
        gssapi_expimp_cred_test \
        gssapi-import-context-test \
        gssapi-import-name \
	gssapi-limited-delegation-test \
	gssapi-delegation-compat-test \
	gssapi-expimp-test \
	gssapi-inquire-sec-ctx-by-oid-test \
	gssapi-acquire-test \
	gssapi-module-test \
        import-cred-test \
        indicate-mechs-test \
        inquire-names-for-mech-test \
	mech-compatibility-test \
	mic-test \
        nonterminated-export-cred-test \
        release-name-test \
        gssapi-thread-test \
	sni-test \
	wrap-test \
        unwrap-null-test

if ENABLE_TESTS
TESTS = $(check_SCRIPTS) \
	alpn-test \
        duplicate-name-test \
	error-test \
        inquire-names-for-mech-test \
	gssapi-anonymous-test \
	gssapi-delegation-test \
        gssapi_expimp_cred_test \
        gssapi-import-context-test \
        gssapi-import-name \
	gssapi-limited-delegation-test \
	gssapi-delegation-compat-test \
	gssapi-expimp-test \
	gssapi-acquire-test \
	gssapi-inquire-sec-ctx-by-oid-test \
        indicate-mechs-test \
	mic-test \
        nonterminated-export-cred-test \
        release-name-test \
	sni-test \
	wrap-test \
        unwrap-null-test

LOG_COMPILER = $(srcdir)/gssapi-test-wrapper

TEST_DATA_DIR = $(srcdir)

if CYGPATH_W_DEFINED
X509_CERT_DIR = $$($(CYGPATH_W) $(abs_builddir))
else
X509_CERT_DIR = $(abs_builddir)
endif

TESTS_ENVIRONMENT = export \
    LIBTOOL="$(LIBTOOL)" \
    X509_USER_CERT=testcred.cert \
    X509_USER_KEY=testcred.key \
    X509_USER_CERT1=testcred-dns1.cert \
    X509_USER_KEY1=testcred-dns1.key \
    X509_USER_CERT2=testcred-dns2.cert \
    X509_USER_KEY2=testcred-dns2.key \
    X509_USER_CERT=testcred.cert \
    X509_USER_KEY=testcred.key \
    X509_USER_CERT_NOCN=testcred-nocn.cert \
    X509_USER_KEY_NOCN=testcred-nocn.key \
    X509_CERT_DIR=$(X509_CERT_DIR) \
    THREAD_MODELS=$(GLOBUS_THREAD_MODELS) \
    TEST_DATA_DIR=$(TEST_DATA_DIR) \
    TEST_CERT_DIR=$(srcdir);

THREAD_TESTS-undefined $(THREAD_TESTS):
	${srcdir}/create-thread-test "$@"
endif

libgssapi_test_utils_la_SOURCES = \
        gssapi_test_utils.c \
        gssapi_test_utils.h

gssapi_anonymous_test_SOURCES = \
        gssapi_anonymous_test.c
gssapi_delegation_test_SOURCES = \
        gssapi_delegation_test.c \
	$(top_srcdir)/library/gssapi_openssl.h \
	$(top_srcdir)/library/globus_gsi_gss_constants.h

gssapi_limited_delegation_test_SOURCES = gssapi_limited_delegation_test.c 
gssapi_delegation_compat_test_SOURCES = gssapi_delegation_compat_test.c

gssapi_thread_test_SOURCES = gssapi_thread_test.c
gssapi_thread_test_LDADD = $(LDADD) $(THREAD_TEST_DLPREOPEN)
gssapi_thread_test_LDFLAGS = -dlpreopen force
gssapi_thread_test_CPPFLAGS = $(AM_CPPFLAGS)

gssapi_import_context_test_SOURCES = gssapi_import_context_test.c
wrap_test_SOURCES =	wrap_test.c 
mic_test_SOURCES =	mic_test.c 
mech_compatibility_test_SOURCES = mech_compatibility_test.c 
gssapi_acquire_test_SOURCES =	gssapi_acquire_test.c
gssapi_expimp_test_SOURCES =	gssapi_expimp_test.c 

gssapi_inquire_sec_ctx_by_oid_test_SOURCES = \
				gssapi_inquire_sec_ctx_by_oid_test.c 

gssapi_module_test_SOURCES = gssapi_module_test.c
gssapi_import_name_SOURCES = gssapi_import_name.c 
compare_name_test_SOURCES = compare_name_test.c
inquire_names_for_mech_test_SOURCES = inquire_names_for_mech_test.c
release_name_test_SOURCES = release_name_test.c
duplicate_name_test_SOURCES = duplicate_name_test.c
indicate_mechs_test_SOURCES = indicate_mechs_test.c
nonterminated_export_cred_test_SOURCES = nonterminated_export_cred_test.c
import_cred_test_SOURCES = import_cred_test.c
import_cred_test_CPPFLAGS = $(AM_CPPFLAGS)

if ENABLE_TESTS
testcred.cacert: testcred.cnf
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

testcred.key testcred-nocn.key testcred-dns1.key testcred-dns2.key:
	umask 077; $(OPENSSL) genrsa -out $@ 2048

testcred.req: testcred.key
	$(OPENSSL) req -subj "/CN=test" -new -key $< -out $@ -config testcred.cnf

testcred-nocn.req: testcred-nocn.key
	$(OPENSSL) req -subj "/userId=test" -new -key $< -out $@ -config testcred.cnf

testcred-dns1.req: testcred-dns1.key
	$(OPENSSL) req -subj "/CN=dns1" -reqexts testcred-dns1 -new -key $< -out $@ -config testcred.cnf

testcred-dns2.req: testcred-dns2.key
	$(OPENSSL) req -subj "/CN=dns2" -reqexts testcred-dns2 -new -key $< -out $@ -config testcred.cnf

.req.cert:
	umask 022; $(OPENSSL) x509 -extensions $* -extfile testcred.cnf -req -passin pass:globus -days 365 -CAkey testcred.cakey -CA testcred.cacert -in $< -out $@

testcred.cert: testcred.srl
testcred-nocn.cert: testcred.cert
testcred-dns1.cert: testcred-nocn.cert
testcred-dns2.cert: testcred-dns1.cert
endif

EXTRA_DIST = \
        $(check_DATA_real) \
        $(check_SCRIPTS) \
        gssapi-test-wrapper \
        create-thread-test \
        gssapi-thread-test-wrapper

CLEANFILES = testcred.key testcred.cert testcred.req \
             testcred-nocn.key testcred-nocn.cert testcred-nocn.req \
             testcred-dns1.cert testcred-dns1.key testcred-dns1.req \
             testcred-dns2.cert testcred-dns2.key testcred-dns2.req \
             testcred.cakey testcred.cacert testcred.srl \
             cert_chain.txt $(THREAD_TESTS)

vhost_setup: $(test_creds)
	mkdir -p vhostdir;
	for credfile in $?; do \
            base=$${credfile%%.*}; \
            mkdir -p vhostdir/$${base}; \
            cp $${credfile} vhostdir/$${base}/$${credfile}.pem;\
        done;
	touch $@

copy_cases:
	if [ "$(VPATH)" != "" ]; then \
            for d in $(check_DATA_real); do \
                cp $(srcdir)/$$d $(builddir); \
            done; \
        fi
	touch $@
clean-local:
	@for linkfile in $(testcred_links)""; do \
	    if [ -f $$linkfile ]; then \
	        rm -f $$(cat $$linkfile) $$linkfile; \
	    fi; \
	done
	@for linkfile in $(signingpolicy_links)""; do \
	    if [ -f $$linkfile ]; then \
	        rm -f $$(cat $$linkfile) $$linkfile; \
	    fi; \
	done
	@if [ "$(VPATH)" != "" ]; then \
            for d in $(check_DATA_real); do \
                if [ -f $(builddir)/$$d ]; then \
                    rm -f $(builddir)/$$d; \
                fi; \
            done; \
        fi
	rm -f copy_cases
	rm -rf vhostdir vhost_setup

SUFFIXES = .key .req .cert .cacert .cakey .link .signing_policy
