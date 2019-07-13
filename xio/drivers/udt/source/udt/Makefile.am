udt_version = 4.11
udt_tarball = udt.sdk.$(udt_version).tar.gz
udt_tarball_url = http://downloads.sourceforge.net/project/udt/udt/$(udt_version)/$(udt_tarball)

privlibdir = $(libdir)/globus
privlib_LTLIBRARIES = libudt.la

libudt_la_SOURCES = globus_udt_config.h
nodist_libudt_la_SOURCES = \
    udt4/src/channel.cpp \
    udt4/src/ccc.h \
    udt4/src/epoll.cpp \
    udt4/src/buffer.cpp \
    udt4/src/common.h \
    udt4/src/core.cpp \
    udt4/src/md5.cpp \
    udt4/src/channel.h \
    udt4/src/list.h \
    udt4/src/list.cpp \
    udt4/src/queue.h \
    udt4/src/api.h \
    udt4/src/packet.h \
    udt4/src/cache.h \
    udt4/src/common.cpp \
    udt4/src/udt.h \
    udt4/src/epoll.h \
    udt4/src/api.cpp \
    udt4/src/ccc.cpp \
    udt4/src/buffer.h \
    udt4/src/core.h \
    udt4/src/packet.cpp \
    udt4/src/cache.cpp \
    udt4/src/queue.cpp \
    udt4/src/window.h \
    udt4/src/md5.h \
    udt4/src/window.cpp
libudt_la_CPPFLAGS = -include $(srcdir)/globus_udt_config.h $(CPPFLAGS)
libudt_la_CXXFLAGS = $(UDT_CXXFLAGS)
libudt_la_LIBADD = $(UDT_LIBADD)
libudt_la_LDFLAGS = -rpath $(privlibdir) -avoid-version -no-undefined

BUILT_SOURCES = $(nodist_libudt_la_SOURCES)
$(BUILT_SOURCES): udt-patch-stamp

$(udt_tarball):
	@$(DOWNLOAD_TO_STDOUT) $(udt_tarball_url) > $(udt_tarball).download; \
	mv $(udt_tarball).download $(udt_tarball)

udt-patch-stamp: udt-unpack-stamp
	patch -p0 < $(srcdir)/globus_udt.diff
	touch $@

udt-unpack-stamp: $(udt_tarball)
	$(AMTAR) -zxf $(udt_tarball)
	touch $@

CLEANFILES = udt-unpack-stamp udt-patch-stamp
DISTCLEANFILES = $(udt_tarball)

EXTRA_DIST = globus_udt.diff

clean-local:
	rm -rf udt4
