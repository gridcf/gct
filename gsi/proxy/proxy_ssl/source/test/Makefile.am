check_PROGRAMS = test_pci
check_DATA = \
	test_pci.norm1.stdout \
	test_pci.norm2.stdout \
	test_pci.norm3.stdout \
	test_pci.norm4.stdout \
	test_pci.norm5.stdout
check_SCRIPTS = TESTS.pl TESTS-junit.pl test_pci.pl

AM_CPPFLAGS = $(OPENSSL_CFLAGS) $(PACKAGE_DEP_CFLAGS) -I$(top_srcdir)/library -I..
LDADD = ../library/libglobus_proxy_ssl.la $(OPENSSL_LIBS) $(PACKAGE_DEP_LIBS) 

test_pci_SOURCES = test_proxycertinfo.c

test:
	chmod a+x $(check_SCRIPTS)
	perl ./TESTS.pl

junit:
	chmod a+x $(check_SCRIPTS)
	perl ./TESTS-junit.pl

TESTS = test_pci.pl

CLEANFILES = \
    test_pci.log1.der \
    test_pci.log2.der \
    test_pci.log3.der \
    test_pci.log4.der \
    test_pci.log5.der \
    test_pci.norm1.der \
    test_pci.norm2.der \
    test_pci.norm3.der \
    test_pci.norm4.der \
    test_pci.norm5.der


EXTRA_DIST = TESTS.pl TESTS-junit.pl test_pci.pl $(check_DATA)
.PHONY: test junit
