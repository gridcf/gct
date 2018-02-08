gettext_version = 0.19.8.1
gettext_tarball = gettext-$(gettext_version).tar.gz
gettext_tarball_url = http://ftp.gnu.org/pub/gnu/gettext/$(gettext_tarball)

all-local: gettext

$(gettext_tarball):
	@$(DOWNLOAD_TO_STDOUT) $(gettext_tarball_url) > $(gettext_tarball).download; \
	mv $(gettext_tarball).download $(gettext_tarball)

gettext-unpack-stamp: $(gettext_tarball)
	$(MKDIR_P) source
	$(AMTAR) -zxf $(gettext_tarball) -C source --strip 1
	touch $@

gettext-configure-stamp: gettext-unpack-stamp
	export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH); \
        cd source && ./configure --prefix=$(abs_builddir) --disable-shared --enable-static CFLAGS="$(CFLAGS) $(lt_prog_compiler_pic)" CPPFLAGS="$(CPPFLAGS)" LDFLAGS="$(LDFLAGS)" CC="$(CC)" CXX="$(CXX)" CXXFLAGS="$(CXXFLAGS) $(lt_prog_compiler_pic)" LD="$(LD)" --host="$(host_triplet)"
	touch $@

gettext: gettext-configure-stamp
	export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH); \
	cd source && $(MAKE) $(AM_MAKEFLAGS) install
	touch $@

CLEANFILES = gettext 
DISTCLEANFILES = $(gettext_tarball) gettext-unpack-stamp gettext-configure-stamp
clean-local:
	test -d source && (cd source && $(MAKE) $(AM_MAKEFLAGS) clean) || true
	rm -rf bin share include lib
distclean-local:
	rm -rf source
