check_PROGRAMS = \
    connect_test \
    data_test \
    get_lingering_close \
    globus_ftp_control_test \
    pipe_test \
    test_server

check_SCRIPTS = get_lingering_close.sh

check_DATA = \
        testcred.key \
        testcred.cert \
        testcred.cakey \
        testcred.cacert \
        testcred.link \
        testcred.signing_policy \
        testcred.srl \
        testcred.gridmap

AM_CPPFLAGS = -I$(top_srcdir) $(PACKAGE_DEP_CFLAGS)
test_ldadd = $(GSI_DRIVER_DLPREOPEN) libtest_common.la ../libglobus_ftp_control.la $(PACKAGE_DEP_LIBS) -lltdl
test_ldflags = -dlpreopen force

check_LTLIBRARIES = libtest_common.la
libtest_common_la_SOURCES = connect_disconnect_test.c test_common.c test_common.h
libtest_common_la_LIBADD = ../libglobus_ftp_control.la $(PACKAGE_DEP_LIBS)

data_test_SOURCES = data_test.c
data_test_LDADD = $(test_ldadd)
data_test_LDFLAGS = $(test_ldflags)

globus_ftp_control_test_SOURCES = \
    abort_test.c \
    async_control_test.c \
    globus_ftp_control_test.c \
    globus_ftp_control_test.h \
    eb_simple_data_test.c \
    outstanding_io_test.c \
    simple_control_test.c \
    simple_data_test.c \
    simple_dir_test.c 

connect_test_LDADD = $(test_ldadd)
connect_test_LDFLAGS = $(test_ldflags)

get_lingering_close_LDADD = $(test_ldadd)
get_lingering_close_LDFLAGS = $(test_ldflags)

globus_ftp_control_test_LDADD = $(test_ldadd)
globus_ftp_control_test_LDFLAGS = $(test_ldflags)

pipe_test_LDADD = $(test_ldadd)
pipe_test_LDFLAGS = $(test_ldflags)

test_server_LDADD = $(test_ldadd)
test_server_LDFLAGS = $(test_ldflags)

if ENABLE_TESTS
TESTS = \
    connect_test \
    data_test \
    pipe_test \
    $(check_SCRIPTS)

if CYGPATH_W_DEFINED
X509_CERT_DIR = $$($(CYGPATH_W) $(abs_builddir))
GRIDMAP = $$($(CYGPATH_W) $(abs_builddir)/testcred.gridmap)
else
X509_CERT_DIR = $(abs_builddir)
GRIDMAP = $(abs_builddir)/testcred.gridmap
endif

TESTS_ENVIRONMENT = export \
    X509_USER_CERT=testcred.cert \
    X509_USER_KEY=testcred.key \
    X509_CERT_DIR=$(X509_CERT_DIR) \
    GRIDMAP=$(GRIDMAP) \
    PATH=$${PATH}:$(abs_builddir):$(abs_srcdir);
LOG_COMPILER = $(LIBTOOL) --mode=execute $(GSI_DRIVER_DLOPEN)


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
	     testcred.cakey testcred.gridmap \
             get_lingering_close.sh.port
clean-local:
	if [ -f testcred.link ]; then \
            rm -f $$(cat testcred.link) testcred.link; \
        fi
	if test -f testcred.signing_policy; then \
	    rm -f $$(cat testcred.signing_policy) testcred.signing_policy; \
	fi
SUFFIXES = .key .req .cert
endif

EXTRA_DIST = $(check_SCRIPTS)
