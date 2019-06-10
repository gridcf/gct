if ENABLE_PYTHON
noinst_LTLIBRARIES = libglobus_net_manager_python.la
else
EXTRA_LTLIBRARIES = libglobus_net_manager_python.la
endif

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_builddir) -I$(top_srcdir)/attr $(PYTHON_CPPFLAGS) $(PACKAGE_DEP_CFLAGS) 
libglobus_net_manager_python_la_SOURCES = globus_net_manager_python.c globus_net_manager_python.h
libglobus_net_manager_python_la_LIBADD = $(PACKAGE_DEP_LIBS) $(PYTHON_LIBS)
