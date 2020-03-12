%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-proxy-utils
%global _name %(echo %{name} | tr - _)
Version:	7.1
Release:	2%{?dist}
Summary:	Grid Community Toolkit - Globus GSI Proxy Utility Programs

Group:		Applications/Internet
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	gcc
BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-openssl-module-devel >= 3
BuildRequires:	globus-gsi-openssl-error-devel >= 2
BuildRequires:	globus-gsi-cert-utils-devel >= 8
BuildRequires:	globus-gsi-sysconfig-devel >= 5
BuildRequires:	globus-gsi-credential-devel >= 5
BuildRequires:	globus-gsi-callback-devel >= 4
BuildRequires:	globus-gsi-proxy-core-devel >= 6
BuildRequires:	globus-gss-assist-devel >= 8
BuildRequires:	globus-gssapi-gsi-devel >= 4
%if %{?suse_version}%{!?suse_version:0}
BuildRequires:	libopenssl-devel
%else
BuildRequires:	openssl-devel
%endif
#		Additional requirements for make check
BuildRequires:	openssl
BuildRequires:	perl-interpreter
BuildRequires:	perl(File::Temp)
BuildRequires:	perl(IO::Handle)
BuildRequires:	perl(strict)
BuildRequires:	perl(Test::More)

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Globus GSI Proxy Utility Programs

%prep
%setup -q -n %{_name}-%{version}

%build
%configure --disable-static \
	   --includedir=%{_includedir}/globus \
	   --libexecdir=%{_datadir}/globus \
	   --docdir=%{_pkgdocdir}

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT

%check
make %{?_smp_mflags} check VERBOSE=1

%files
%defattr(-,root,root,-)
%{_bindir}/grid-cert-diagnostics
%{_bindir}/grid-proxy-destroy
%{_bindir}/grid-proxy-info
%{_bindir}/grid-proxy-init
%doc %{_mandir}/man1/grid-cert-diagnostics.1*
%doc %{_mandir}/man1/grid-proxy-destroy.1*
%doc %{_mandir}/man1/grid-proxy-info.1*
%doc %{_mandir}/man1/grid-proxy-init.1*
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%changelog
* Thu Mar 12 2020 Mattias Ellert <mattias.ellert@physics.uu.se> - 7.1-2
- Add BuildRequires perl-interpreter
- Add additional perl dependencies for tests

* Sat May 05 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 7.1-1
- Increase default proxy key size to 2048 bits
- Use 2048 bit RSA key for tests

* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 7.0-1
- First Grid Community Toolkit release
- Remove support for openssl101e (RHEL5 is EOL)

* Fri Jan 06 2017 Globus Toolkit <support@globus.org> - 6.19-1
- Fix RSA key checking

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 6.18-1
- Update for el.5 openssl101e, replace docbook with asciidoc

* Mon Aug 29 2016 Globus Toolkit <support@globus.org> - 6.17-3
- Updates for SLES 12

* Thu Aug 18 2016 Globus Toolkit <support@globus.org> - 6.17-1
- Makefile fixes

* Tue Aug 16 2016 Globus Toolkit <support@globus.org> - 6.16-1
- Updates for OpenSSL 1.1.0

* Mon Mar 14 2016 Globus Toolkit <support@globus.org> - 6.15-1
- Updates for reverse lookups for backward compatibility checking

* Wed Mar 09 2016 Globus Toolkit <support@globus.org> - 6.14-1
- Missing handle_init in grid-cert-diagnostics -c
- Add option (-H) to compare hostname when checking a certificate with -c

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 6.13-2
- Add vendor

* Wed Jul 29 2015 Globus Toolkit <support@globus.org> - 6.13-1
- Add missing globus-gssapi-gsi dependency

* Tue Jul 28 2015 Globus Toolkit <support@globus.org> - 6.12-1
- Add explicit name comparison result and mode select option

* Wed Jul 01 2015 Globus Toolkit <support@globus.org> - 6.11-1
- Remove unused label
- Check for c99 compiler flags

* Wed Jul 01 2015 Globus Toolkit <support@globus.org> - 6.10-1
- GT-607: improve grid-cert-diagnostic command to retrieve endpoint cert

* Thu Sep 25 2014 Globus Toolkit <support@globus.org> - 6.9-1
- Remove unused Doxygen headers
- Quiet some autoconf/automake warnings
- Convert manpages to asciidoc source

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 6.8-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 6.7-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 6.7-1
- Merge changes from Mattias Ellert

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 6.6-1
- Version bump for consistency

* Tue Feb 25 2014 Globus Toolkit <support@globus.org> - 6.5-1
- Packaging fixes

* Sat Feb 01 2014 Globus Toolkit <support@globus.org> - 6.4-1
- Fix test wrapper with old automake

* Sat Feb 01 2014 Globus Toolkit <support@globus.org> - 6.3-1
- Fix test cred permissions

* Sat Feb 01 2014 Globus Toolkit <support@globus.org> - 6.2-1
- version update

* Mon Jan 27 2014 Globus Toolkit <support@globus.org> - 6.0-1
- Add tests to globus_proxy_utils
- Doxygen / header cleanup
- Native debian package updates
- New version of rectify-versions
- Opt for POSIX 1003.1-2001 (pax) format tarballs
- Remove GPT and make-packages.pl from build process
- Remove GPT metadata
- autoconf/automake updates

* Thu Jan 23 2014 Globus Toolkit <support@globus.org> - 6.1-1
- Add openssl dependency

* Thu Jan 23 2014 Globus Toolkit <support@globus.org> - 6.0-1
- Repackage for GT6 without GPT

* Tue Sep 10 2013 Globus Toolkit <support@globus.org> - 5.2-1
- GT-387: grid-proxy-init -pwstdin reads too many characters

* Mon Jul 08 2013 Globus Toolkit <support@globus.org> - 5.1-3
- openssl-libs for newer fedora

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 5.1-2
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Wed May 15 2013 Globus Toolkit <support@globus.org> - 5.1-1
- GT-272: Increase default proxy key size

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 5.0-10
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 5.0-9
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 5.0-8
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 5.0-7
- RHEL 4 patches

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 5.0-6
- Updated version numbers

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 5.0-5
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 5.0-4
- Last sync prior to 5.2.0

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 5.0-3
- Add explicit dependencies on >= 5.2 libraries

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 5.0-2
- Update for 5.1.2 release

* Sat Jul 17 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 3.9-1
- Update to Globus Toolkit 5.0.2
- Drop patch globus-proxy-utils-oid.patch (fixed upstream)

* Mon May 31 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 3.7-2
- Fix OID registration pollution

* Wed Apr 14 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 3.7-1
- Update to Globus Toolkit 5.0.1
- Drop patches globus-proxy-utils-ldflag-overwrt.patch and
  globus-proxy-utils-deps.patch (fixed upstream)

* Fri Jan 22 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 3.5-1
- Update to Globus Toolkit 5.0.0

* Fri Aug 21 2009 Tomas Mraz <tmraz@redhat.com> - 2.5-4
- rebuilt with new openssl

* Thu Jul 23 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.5-3
- Add instruction set architecture (isa) tags

* Wed Jun 03 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.5-2
- Update to official Fedora Globus packaging guidelines

* Thu Apr 16 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.5-1
- Make comment about source retrieval more explicit
- Change defines to globals
- Remove explicit requires on library packages
- Put GLOBUS_LICENSE file in extracted source tarball

* Sun Mar 15 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.5-0.5
- Adapting to updated globus-core package

* Thu Feb 26 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.5-0.4
- Add s390x to the list of 64 bit platforms

* Tue Dec 30 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.5-0.3
- Adapt to updated GPT package

* Wed Oct 15 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.5-0.2
- Update to Globus Toolkit 4.2.1

* Mon Jul 14 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.1-0.1
- Autogenerated
