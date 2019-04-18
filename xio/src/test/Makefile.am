SUBDIRS = drivers .

check_PROGRAMS_NO_SCRIPT = server_pre_init_test

check_PROGRAMS =                        \
	framework_test			\
	http_header_test		\
	http_post_test			\
	http_put_test			\
	http_get_test			\
	http_pingpong_test		\
	http_timeout_test 		\
        handle_create_from_url_test     \
	http_throughput_test            \
	$(check_PROGRAMS_NO_SCRIPT)

check_DATA =                            \
	headers				\
	multi-line-header		\
	long-headers			\
	multi-headers			\
	large-file                      \
        test-common.pl

check_SCRIPTS =                         \
	basic-test.pl                   \
	close-barrier-test.pl           \
	close-cancel-test.pl            \
	failure-test.pl                 \
	http-get-test.pl		\
	http-header-test.pl		\
	http-post-test.pl		\
	http-put-test.pl		\
        http-pingpong-test.pl		\
	http-timeout-test.pl		\
	http-throughput-test.pl         \
	read-barrier-test.pl            \
	timeout-test.pl                 \
	cancel-test.pl                  \
	random-test.pl                  \
	server-test.pl                  \
	verify-test.pl                  \
	attr-test.pl                    \
	space-test.pl                   \
	server2-test.pl                 \
	block-barrier-test.pl           \
	stack-test.pl                   \
	unload-test.pl

if CYGPATH_W_DEFINED
TEST_DATA_DIR = $$($(CYGPATH_W) $(srcdir)/)
else
TEST_DATA_DIR = $(srcdir)/
endif

TESTS = $(check_SCRIPTS) $(check_PROGRAMS_NO_SCRIPT)
TESTS_ENVIRONMENT = \
	export XIO_TEST_OUPUT_DIR=test_output/$$$$ \
	       TEST_DATA_DIR=$(TEST_DATA_DIR) ;

AM_CPPFLAGS = \
    -I$(top_srcdir) \
    -I$(XIO_BUILTIN_PC_INCLUDES) \
    -I$(srcdir)/drivers \
    -DGLOBUS_BUILTIN=1 $(PACKAGE_DEP_CFLAGS)

LDADD = $(PACKAGE_DEP_LIBS) \
	-dlpreopen drivers/libglobus_xio_test_drivers.la \
	../libglobus_xio.la \
        -lltdl

framework_test_SOURCES =                \
	test_common.h                   \
	test_common.c                   \
	driver_stack.c                  \
	blocking_dd_test.c              \
	unload_test.c                   \
	framework_test.c                \
	space_test.c                    \
	timeout_test.c                  \
	close_barrier_test.c            \
	close_barrier2_test.c           \
	close_cancel_test.c             \
	cancel_test.c                   \
	server2_test.c                  \
	attr_test.c                     \
	block_barrier_test.c            \
	read_barrier_test.c

HTTP_COMMON_Sources = http_test_common.h http_test_common.c

HTTP_PERFORMANCE_Sources = globus_utp.h globus_utp_private.h globus_utp_main.c http_performance_common.h http_performance_common.c

http_header_test_SOURCES = http_header_test.c $(HTTP_COMMON_Sources)
http_post_test_SOURCES = http_post_test.c $(HTTP_COMMON_Sources)
http_put_test_SOURCES = http_put_test.c $(HTTP_COMMON_Sources)
http_get_test_SOURCES = http_get_test.c $(HTTP_COMMON_Sources)
http_pingpong_test_SOURCES = http_pingpong_test.c $(HTTP_PERFORMANCE_Sources)
http_timeout_test_SOURCES = http_timeout_test.c $(HTTP_COMMON_Sources)
http_throughput_test_SOURCES = http_throughput_test.c $(HTTP_PERFORMANCE_Sources)

clean-local:
	rm -rf *timings.txt test_output

EXTRA_DIST =                    \
        $(check_SCRIPTS)        \
        $(check_DATA)
