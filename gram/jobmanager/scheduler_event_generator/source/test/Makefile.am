check_LTLIBRARIES =  \
    libglobus_seg_load_test_module.la \
    libglobus_seg_timestamp_test_module.la
check_PROGRAMS = \
    seg-module-load-test \
    seg-timestamp-test \
    seg-api-test
check_SCRIPTS = \
    seg-api-test.pl
check_DATA = \
    seg_api_test_data.txt \
    test-data.txt

TESTS = seg-api-test.pl seg-module-load-test seg-timestamp-test

AM_CPPFLAGS = -I$(top_srcdir) $(PACKAGE_DEP_CFLAGS)

libglobus_seg_load_test_module_la_SOURCES = \
    seg_module_load_test_module.c
libglobus_seg_load_test_module_la_LDFLAGS = \
	-module \
	-no-undefined \
	-avoid-version \
	-rpath $(abs_builddir)
libglobus_seg_load_test_module_la_LIBADD = ../libglobus_scheduler_event_generator.la $(PACKAGE_DEP_LIBS)

libglobus_seg_timestamp_test_module_la_SOURCES = \
    seg_timestamp_test_module.c
libglobus_seg_timestamp_test_module_la_LDFLAGS = \
	-module \
	-no-undefined \
	-avoid-version \
	-rpath $(abs_builddir)
libglobus_seg_timestamp_test_module_la_LIBADD = ../libglobus_scheduler_event_generator.la $(PACKAGE_DEP_LIBS)

seg_module_load_test_SOURCES = seg_module_load_test.c
seg_module_load_test_LDADD = \
        -dlpreopen libglobus_seg_load_test_module.la \
	../libglobus_scheduler_event_generator.la \
	$(PACKAGE_DEP_LIBS)

seg_timestamp_test_SOURCES = seg_timestamp_test.c
seg_timestamp_test_LDADD = \
        -dlpreopen libglobus_seg_timestamp_test_module.la \
	../libglobus_scheduler_event_generator.la \
	$(PACKAGE_DEP_LIBS)

seg_api_test_SOURCES = seg_api_test.c globus_scheduler_event_generator_stdout.c globus_scheduler_event_generator_stdout.h
seg_api_test_LDADD = \
	../libglobus_scheduler_event_generator.la \
	$(PACKAGE_DEP_LIBS)

EXTRA_DIST = seg_test.dox $(check_DATA) $(check_SCRIPTS) make-test-data.pl
