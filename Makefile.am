PKG_CONFIG_PATH = $(GT_PKGCONFIG_DIRS)
SUBDIRS = @TOP_SUBDIRS@
DIST_SUBDIRS = @TOP_DIST_SUBDIRS@

@TARGETS_FILE@

$(GT_TARGETS_CLEAN):
	@if [ -f $($@_stamp) ]; then \
		export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) ; cd $($@_SUBDIR) && $(MAKE) $(AM_MAKEFLAGS) clean ; \
	fi
	@rm -f $($@_stamp)
$(GT_ALL_TARGETS_INSTALL):
	@export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) ; cd $($@_SUBDIR) && $(MAKE) $(AM_MAKEFLAGS) install
$(GT_TARGETS_CHECK):
	@export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) ; cd $($@_SUBDIR) && $(MAKE) $(AM_MAKEFLAGS) check
$(GT_ALL_TARGETS_DIST):
	@$(MKDIR_P) package-output
	@export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) ; cd $($@_SUBDIR) && MAKEFLAGS= $(MAKE) $(AM_MAKEFLAGS) dist
	@cp $($@_SUBDIR)/$($@-tarball) package-output

$(GT_ALL_STAMPS):
	@export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) ; ( (cd $($@_SUBDIR) && $(MAKE) $(AM_MAKEFLAGS) all) && touch $@) || exit 1

$(GT_ONLY):
	@export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) ; cd $($@_SUBDIR) && $(MAKE) $(AM_MAKEFLAGS) all

RPMDIRS = BUILD BUILDROOT RPMS SOURCES SPECS SRPMS

.rpmmacros:
	@$(MKDIR_P) rpmbuild
	@(case `lsb_release -is`:`lsb_release -rs` in \
		CentOS*:5*|RedHat*:5*|Scientific*:5*) \
			echo "%rhel 5" \
			;; \
		CentOS*:6*|RedHat*:6*|Scientific*:6*) \
			echo "%rhel 6" \
			;; \
		CentOS*:7*|RedHat*:7*|Scientific*:7*) \
			echo "%rhel 7" \
			;; \
	  esac; \
	  echo "%_topdir $(abs_builddir)/rpmbuild"; \
	  echo "%globus_version $(PACKAGE_VERSION)"; \
	) > $@


$(GT_ALL_TARGETS_RPM): .rpmmacros
	@for d in $(RPMDIRS); do mkdir -p rpmbuild/$$d; done
	@cp $($@_SOURCE) rpmbuild/SOURCES
	@HOME=$$(pwd) ac_cv_host=$(host_triplet) rpmbuild --target=$(host_triplet) -ba $($@_SPECFILE)

$(GT_ALL_TARGETS_DEB):
	@rm -rf debbuild/$($@_PACKAGE_NAME)
	@mkdir -p debbuild/$($@_PACKAGE_NAME)
	@cp $($@_SOURCE) $($@_TARBALL)
	@cp -R $(srcdir)/packaging/debian/$($@_PACKAGE_NAME) debbuild
	@sed -e 's/@distro@/$(shell lsb_release -cs)/g' <  $(srcdir)/packaging/debian/$($@_PACKAGE_NAME)/debian/changelog.in > debbuild/$($@_PACKAGE_NAME)/debian/changelog
	@tar --strip 1 -zxf $($@_SOURCE) -C debbuild/$($@_PACKAGE_NAME)
	@cd debbuild/$($@_PACKAGE_NAME) && dpkg-buildpackage -uc -us $(DPKG_BUILDPACKAGE_OPTS)

$(GT_ALL_TARGETS_OSX_PKG):
	rm -f $($@)
	export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) ; cd $($($@)_SUBDIR) && $(MAKE) $(AM_MAKEFLAGS) install DESTDIR=$(abs_builddir)/osx/dest/$$(basename $($@) .pkg)
	@for filename in `find $(abs_builddir)/osx/dest/$$(basename $($@) .pkg) -type f | grep -v '\.a$$'`; do \
	    if file "$$filename" | grep -q Mach-O; then \
		echo "Processing $$filename"; \
		for lib in `otool -L "$$filename" | sed 1d | grep -E "$(libdir)|ltdl" | awk '{print $$1}'`; do \
		    echo "Rewriting $$lib"; \
                    libname="$$(echo "$$lib" | sed -e 's|//*|/|g' -e 's|${libdir}//*||')"; \
		    install_name_tool -change "$$lib" "@loader_path/../lib/$$libname" $$filename; \
	       	done; \
		if basename $$filename | grep -vq '^lib'; then \
		    install_name_tool -add_rpath "@loader_path/../lib" $$filename; \
		else \
		    install_name_tool -id "$$(basename $$filename)" "$$filename"; \
		fi; \
	    fi; \
	done
	@find osx/dest/$$(basename $($@) .pkg) \( -name '*.la' -o -name '*.a' \) -a -exec rm {} +
	@mkdir -p osx/packages
	@pkgbuild --root $(abs_builddir)/osx/dest/$$(basename $($@) .pkg) \
            --version $($($@)_PACKAGE_VERSION) \
	    --identifier org.globus.$($($@)_PACKAGE_NAME) \
            --ownership recommended \
	    $($($($@)_PACKAGE_NAME)_macosx_scripts) \
            $($@)

if !HAVE_LTDL
LTDL_MAC_PKG = ltdl-@LTDL_VERSION@.pkg
GT_TARGETS_OSX_PKG += $(LTDL_MAC_PKG)
endif
globus_common_macosx_scripts = --scripts osx/scripts/globus_common
$(globus_common-osx-pkg): osx/scripts/globus_common/postinstall

osx/$(PACKAGE)-$(VERSION).pkg: $(GT_TARGETS_OSX_PKG) osx/distribution.xml
	@productbuild --distribution osx/distribution.xml \
		--package-path osx/packages \
		--resources $(srcdir)/osx/resources \
		$@
osx-pkgs: osx/$(PACKAGE)-$(VERSION).pkg
rpm-pkgs: $(GT_TARGETS_RPM)
tarballs: $(GT_ALL_TARGETS_DIST)

DOC_STAMPS =
DOC_PHONY = 
DOC_CLEAN = 
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp
DOC_CLEAN += doc-clean

doc: doc-stamp
doc-stamp: Doxyfile doxygen/index.dox $(GT_ENABLED_BUNDLES)
	rm -rf doc
	@$(DOXYGEN)
	@touch $@
doc-clean:
	@rm -rf doc doc-stamp
DOC_PHONY += doc doc-clean
endif

install-data-local: $(DOC_STAMPS)
	@if test -d doc/man/man3; then \
		install -d -m 755 $(DESTDIR)$(mandir)/man3; \
		$(INSTALL) -m 644 doc/man/man3/[Gg][Ll][Oo][Bb][Uu][Ss]*.3 $(DESTDIR)$(mandir)/man3 ; \
	fi
	@if test -d doc/html; then \
		for dir in `(cd doc; find html -type d)`; do \
			install -d -m 755 $(DESTDIR)$(docdir)/$$dir; \
		done ; \
		for f in `(cd doc; find html -type f)`; do \
			$(INSTALL) -m 644 doc/$$f $(DESTDIR)$(docdir)/$$f ; \
		done ; \
	fi

install-exec-local:
	@for stamp in $(GT_ALL_STAMPS); do \
	    if [ -f $$stamp ]; then \
		stamp_pkg=`echo $$stamp | sed -e s'/-stamp//'`; \
	        export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) ; $(MAKE) $(AM_MAKEFLAGS) $${stamp_pkg}-install ; \
	    fi; \
	done
EXTRA_DIST=doxygen/index.dox globus-vararg-enums-doxygen-filter.pl GLOBUS_LICENSE osx/resources/GLOBUS_LICENSE  osx/resources/logo.png


if !HAVE_LTDL
LTDL_CLEAN = libltdl-clean
endif

all-local: $(GT_ENABLED_BUNDLES) $(DOC_STAMPS)
clean-local: $(GT_TARGETS_CLEAN) $(DOC_CLEAN) $(LTDL_CLEAN)
distclean-local: $(GT_TARGETS_DISTCLEAN)
check-local:
	@for stamp in $(GT_ALL_STAMPS); do \
	    if [ -f $$stamp ]; then \
		stamp_pkg=`echo $$stamp | sed -e s'/-stamp//'`; \
	        export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) ; $(MAKE) $(AM_MAKEFLAGS) $${stamp_pkg}-check ; \
	    fi; \
	done

libltdl-install:
if !HAVE_LTDL
	PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) ; cd libltdl; $(MAKE) $(AM_MAKEFLAGS) install
endif
	:
libltdl-stamp:
if !HAVE_LTDL
	PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) ; cd libltdl; $(MAKE) $(AM_MAKEFLAGS)
endif
	touch $@

if !HAVE_LTDL
libltdl-clean:
	PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) ; if [ -f libltdl/Makefile ]; then cd libltdl; $(MAKE) $(AM_MAKEFLAGS) clean; fi
	rm -f libltdl-stamp
endif

# GSI-OpenSSH doesn't have a working "dist" makefile rule.
# We'll preserve the output from the prep-gsissh so that
# configuration will work without re-downloading the sources
dist-hook:
	$(MKDIR_P) $(distdir)/gsi_openssh $(distdir)/myproxy $(distdir)/packaging/package-output
	$(INSTALL) -p $(srcdir)/prep-gsissh $(distdir)
	$(INSTALL) -p $(srcdir)/packaging/package-output/* $(distdir)/packaging/package-output
	(cd $(distdir); ./prep-gsissh;)
	(if [ ! -d $(distdir)/gridftp/hdfs ]; then cp -pPR $(srcdir)/gridftp/hdfs $(distdir)/gridftp/hdfs; fi )
	(if [ ! -d $(distdir)/myproxy/source ]; then cp -pPR $(srcdir)/myproxy/source $(distdir)/myproxy/source; fi )
	

globus_gridftp_server-stamp: $(GT_ENABLED_XIO_DRIVERS)
.PHONY: $(GT_TARGETS_ALL) $(GT_TARGETS_ONLY) $(GT_TARGETS_CLEAN) $(GT_TARGETS_INSTALL) $(GT_TARGETS_CHECK) $(GT_TARGETS_DISTCLEAN) $(GT_TARGETS_DIST) $(DOC_PHONY) $(GT_TARGETS_RPM) $(GT_TARGETS_DEB) $(GT_BUNDLES)
