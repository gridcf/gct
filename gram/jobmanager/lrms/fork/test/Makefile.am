SUBDIRS = seg

test_SCRIPTS = TESTS.pl TESTS-junit.pl


test: all
	perl "-I@perlmoduledir@" TESTS.pl

junit: all
	perl "-I@perlmoduledir@" TESTS-junit.pl

EXTRA_DIST = dirt.sh TESTS.pl.in TESTS-junit.pl.in

.PHONY: junit test
