include_HEADERS = \
        globus_config.h \
        globus_types.h \
        globus_release.h \
        globus_module.h \
        globus_url.h \
        globus_states.h \
        globus_list.h \
        globus_hashtable.h \
        globus_fifo.h \
        globus_symboltable.h \
        globus_logging.h    \
        globus_object.h \
        globus_object_hierarchy.h \
        globus_error.h \
        globus_error_hierarchy.h \
        globus_error_string.h \
        globus_error_generic.h \
        globus_error_errno.h \
        globus_time.h \
        globus_thread_pool.h \
        globus_handle_table.h \
        globus_callback.h \
        globus_options.h \
        globus_memory.h \
        globus_print.h \
        globus_priority_q.h \
        globus_range_list.h \
        globus_tilde_expand.h \
        globus_libc.h \
        globus_debug.h \
        globus_args.h \
        globus_preload.h \
        globus_strptime.h \
        globus_thread_common.h \
        globus_thread_rw_mutex.h \
        globus_thread_rmutex.h \
        globus_extension.h \
        globus_common.h \
        globus_common_include.h \
        globus_thread.h \
        globus_uuid.h


pthreads_sources = globus_thread_pthreads.c
windowsthreads_sources = globus_thread_windows.h globus_thread_windows.c

thread_libs = 
if BUILD_PTHREADS
thread_libs += libglobus_thread_pthread.la
endif
if BUILD_WINDOWS_THREADS
thread_libs += libglobus_thread_windows.la
endif

lib_LTLIBRARIES = libglobus_common.la $(thread_libs) libglobus_memory_debug.la

AM_CPPFLAGS = \
    -DGLOBUS_PREFIX=\"${prefix}\" \
    -DGLOBUS_LIBDIR=\"${libdir}\" \
    -I $(builddir) \
    $(PACKAGE_DEP_CFLAGS)

libglobus_memory_debug_la_LIBADD = $(PACKAGE_DEP_LIBS) libglobus_common.la $(LIBS)
libglobus_memory_debug_la_CPPFLAGS = \
        $(AM_CPPFLAGS) \
        -DGLOBUS_MEMORY_DEBUG_LEAKS
libglobus_memory_debug_la_SOURCES = \
        globus_memory.c
libglobus_memory_debug_la_LDFLAGS = \
        -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined 
libglobus_memory_debug_la_DEPENDENCIES = libglobus_common.la
libglobus_common_la_LIBADD = $(LT_LIBOBJS) $(PACKAGE_DEP_LIBS) $(LIBS)
libglobus_common_la_SOURCES = \
        globus_args.c \
        globus_args.h \
        globus_callback.c \
        globus_callback_nothreads.c \
        globus_callback_threads.c \
        globus_callback.h \
        globus_config.h \
        globus_options.c \
        globus_options.h \
        globus_i_callback.h \
        globus_common_paths.c \
        globus_common.c \
        globus_debug.c \
        globus_debug.h \
        globus_error.c \
        globus_error.h \
        globus_error_hierarchy.c \
        globus_error_hierarchy.h \
        globus_error_generic.c \
        globus_i_error_generic.c \
        globus_error_generic.h \
        globus_i_error_generic.h \
        globus_error_errno.c \
        globus_i_error_errno.c \
        globus_error_errno.h \
        globus_fifo.c \
        globus_fifo.h \
        globus_handle_table.c \
        globus_handle_table.h \
        globus_hashtable.c \
        globus_hashtable.h \
        globus_i_thread.h \
        globus_libc.c \
        globus_libc.h \
        globus_libc_setenv.c \
        globus_list.c \
        globus_list.h \
        globus_logging.c \
        globus_logging.h \
        globus_memory.c \
        globus_memory.h \
        globus_module.c \
        globus_module.h \
        globus_object.c \
        globus_object.h \
        globus_object_cache.c \
        globus_object_cache.h \
        globus_object_hierarchy.c \
        globus_object_hierarchy.h \
        globus_print.c \
        globus_print.h \
        globus_priority_q.c \
        globus_priority_q.h \
        globus_range_list.c \
        globus_range_list.h \
        globus_release.h \
        globus_strptime.c \
        globus_strptime.h \
        globus_symboltable.c \
        globus_symboltable.h \
        globus_thread_common.c \
        globus_thread_common.h \
        globus_thread.c \
        globus_thread.h \
        globus_thread_pool.c \
        globus_thread_pool.h \
        globus_thread_rw_mutex.c \
        globus_thread_rw_mutex.h \
        globus_thread_rmutex.c \
        globus_thread_rmutex.h \
        globus_tilde_expand.c \
        globus_time.c \
        globus_tilde_expand.h \
        globus_time.h \
        globus_url.c \
        globus_url.h \
        globus_states.h \
        globus_states.c \
        globus_error_string.c \
        globus_error_string.h \
        globus_extension.c \
        globus_extension.h \
        globus_common_include.h \
        globus_uuid.c \
        globus_uuid.h \
        globus_thread_none.c
libglobus_common_la_LDFLAGS = \
        -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined 

if USE_SYMBOL_LABELS
libglobus_common_la_LIBADD += -lpthread
libglobus_common_la_LDFLAGS += -Wl,--version-script,sym
if USES_LTO
libglobus_common_la_LDFLAGS += -flto-partition=none
endif
endif

if BUILD_PTHREADS
libglobus_thread_pthread_la_LIBADD = libglobus_common.la $(PACKAGE_DEP_LIBS) -lpthread
libglobus_thread_pthread_la_SOURCES = globus_thread_pthreads.c
libglobus_thread_pthread_la_LDFLAGS = -module -no-undefined -avoid-version
endif

if BUILD_WINDOWS_THREADS
libglobus_thread_windows_la_LIBADD = libglobus_common.la $(PACKAGE_DEP_LIBS)
libglobus_thread_windows_la_SOURCES = globus_thread_windows.c globus_thread_windows.h
libglobus_thread_windows_la_LDFLAGS = -module -no-undefined -avoid-version
endif

EXTRA_DIST = \
        $(pthreads_sources) \
        $(windowsthreads_sources) \
        sym

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_common_la_SOURCES)
	rm -rf doc
	$(DOXYGEN)
	touch $@

clean-local:
	rm -rf doc doc-stamp
endif

all-local: $(DOC_STAMPS)

install-data-local: $(DOC_STAMPS)
	if test -d doc/man/man3; then \
		install -d -m 755 $(DESTDIR)$(mandir)/man3; \
		$(INSTALL) -m 644 doc/man/man3/[Gg][Ll][Oo][Bb][Uu][Ss]*.3 $(DESTDIR)$(mandir)/man3 ; \
	fi
	if test -d doc/html; then \
		for dir in `(cd doc; find html -type d)`; do \
			install -d -m 755 $(DESTDIR)$(docdir)/$$dir; \
		done ; \
		for f in `(cd doc; find html -type f)`; do \
			$(INSTALL) -m 644 doc/$$f $(DESTDIR)$(docdir)/$$f ; \
		done ; \
	fi
