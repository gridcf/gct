%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-gsi-callback
%global soname 0
%global _name %(echo %{name} | tr - _)
Version:	6.2
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Globus GSI Callback Library

Group:		System Environment/Libraries
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	gcc
BuildRequires:	globus-openssl-module-devel >= 3
BuildRequires:	globus-gsi-openssl-error-devel >= 2
BuildRequires:	globus-gsi-cert-utils-devel >= 8
BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-gsi-sysconfig-devel >= 5
%if %{?suse_version}%{!?suse_version:0}
BuildRequires:	libopenssl-devel
%else
BuildRequires:	openssl-devel
%endif
BuildRequires:	doxygen

%if %{?suse_version}%{!?suse_version:0}
%global mainpkg lib%{_name}%{soname}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0}
%package %{?nmainpkg}
Summary:	Grid Community Toolkit - Globus GSI Callback Library
Group:		System Environment/Libraries
Provides:	%{name} = %{version}-%{release}
Obsoletes:	%{name} < %{version}-%{release}
%endif

%package devel
Summary:	Grid Community Toolkit - Globus GSI Callback Library Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}

%package doc
Summary:	Grid Community Toolkit - Globus GSI Callback Library Documentation Files
Group:		Documentation
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6
BuildArch:	noarch
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0}
%description %{?nmainpkg}
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{mainpkg} package contains:
Globus GSI Callback Library
%endif

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Globus GSI Callback Library

%description devel
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-devel package contains:
Globus GSI Callback Library Development Files

%description doc
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-doc package contains:
Globus GSI Callback Library Documentation Files

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

# Remove libtool archives (.la files)
rm $RPM_BUILD_ROOT%{_libdir}/*.la

%post %{?nmainpkg} -p /sbin/ldconfig

%postun %{?nmainpkg} -p /sbin/ldconfig

%files %{?nmainpkg}
%defattr(-,root,root,-)
%{_libdir}/libglobus_gsi_callback.so.*
%{_libdir}/libglobus_oldgaa.so.*
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*
%{_libdir}/libglobus_gsi_callback.so
%{_libdir}/libglobus_oldgaa.so
%{_libdir}/pkgconfig/%{name}.pc

%files doc
%defattr(-,root,root,-)
%doc %{_mandir}/man3/*
%dir %{_pkgdocdir}
%dir %{_pkgdocdir}/html
%doc %{_pkgdocdir}/html/*
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%changelog
* Tue Mar 10 2020 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.2-1
- Make makefiles exit sooner on errors

* Wed Nov 21 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.1-1
- Doxygen fixes

* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.0-1
- First Grid Community Toolkit release
- Remove support for openssl101e (RHEL5 is EOL)

* Fri Jan 06 2017 Globus Toolkit <support@globus.org> - 5.13-1
- Fix crash with OpenSSL 1.1.0 with a CRL (issue #87), replace some deprecated API calls

* Wed Oct 26 2016 Globus Toolkit <support@globus.org> - 5.12-1
- Fix crash with empty X.509 subject name
- Ensure that X509_OBJECT_new compatibility macro initializes the object

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 5.11-1
- Update for el.5 openssl101e

* Tue Aug 30 2016 Globus Toolkit <support@globus.org> - 5.10-1
- Fix path length constraint handling

* Thu Aug 25 2016 Globus Toolkit <support@globus.org> - 5.9-3
- Updates for SLES 12

* Tue Aug 16 2016 Globus Toolkit <support@globus.org> - 5.9-1
- Updates for OpenSSL 1.1.0

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 5.8-2
- Add vendor

* Wed Jul 01 2015 Globus Toolkit <support@globus.org> - 5.8-1
- GT-608: Remove dead code from gsi callback

* Thu May 28 2015 Globus Toolkit <support@globus.org> - 5.7-1
- GT-599: Race condition in globus_gsi_callback_get_SSL_callback_data_index() and globus_gsi_callback_get_X509_STORE_callback_data_index()

* Wed Sep 24 2014 Globus Toolkit <support@globus.org> - 5.6-1
- Doxygen markup fixes
- Include more manpages for API
- Fix dependency version
- Fix typos and clarify some documentation

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 5.5-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 5.4-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 5.4-1
- Merge changes from Mattias Ellert

* Tue May 27 2014 Globus Toolkit <support@globus.org> - 5.3-1
- Use package-named config.h

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 5.2-1
- Version bump for consistency

* Mon Feb 10 2014 Globus Toolkit <support@globus.org> - 5.1-1
- Packaging fixes

* Mon Jan 27 2014 Globus Toolkit <support@globus.org> - 5.0-1
- Remove GPT and make-packages.pl from build process

* Fri Sep 13 2013 Globus Toolkit <support@globus.org> - 4.6-1
- GT-426: memory leaks in globus-gsi-callback package

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 4.5-2
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Mon Mar 18 2013 Globus Toolkit <support@globus.org> - 4.5-1
- GT-354: Compatibility with automake 1.13

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 4.4-3
- Workaround missing F18 doxygen/latex dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 4.4-2
- 5.2.3

* Wed Jul 25 2012 Joseph Bester <bester@mcs.anl.gov> - 4.4-1
- GT-235: GSI does not reload CRLs if they are replaced

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 4.3-3
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 4.3-2
- GT 5.2.2 Release

* Wed Jun 27 2012 Joseph Bester <bester@mcs.anl.gov> - 4.3-1
- GT-165: Threaded server has a race condition with parallel data channels and loading crls
- GT-166: Threaded server data channel connection error
- RIC-224: Eliminate some doxygen warnings
- RIC-226: Some dependencies are missing in GPT metadata
- RIC-227: Potentially unsafe format strings in GSI

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 4.2-3
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 4.2-2
- SLES 11 patches

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 4.2-1
- RIC-224: Eliminate some doxygen warnings
- RIC-226: Some dependencies are missing in GPT metadata
- RIC-227: Potentially unsafe format strings in GSI

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 4.1-4
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 4.1-3
- Last sync prior to 5.2.0

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 4.1-2
- Add explicit dependencies on >= 5.2 libraries

* Thu Oct 06 2011 Joseph Bester <bester@mcs.anl.gov> - 4.1-1
- Add backward-compatibility aging

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 4.0-2
- Update for 5.1.2 release

* Sat Jul 17 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.7-1
- Update to Globus Toolkit 5.0.2
- Drop patch globus-gsi-callback-oid.patch (fixed upstream)

* Mon May 31 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.5-2
- Fix OID registration pollution

* Fri Jan 22 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.5-1
- Update to Globus Toolkit 5.0.0

* Fri Aug 21 2009 Tomas Mraz <tmraz@redhat.com> - 1.10-4
- rebuilt with new openssl

* Thu Jul 23 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.10-3
- Add instruction set architecture (isa) tags
- Make doc subpackage noarch

* Wed Jun 03 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.10-2
- Update to official Fedora Globus packaging guidelines

* Thu Apr 16 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.10-1
- Make comment about source retrieval more explicit
- Change defines to globals
- Remove explicit requires on library packages
- Put GLOBUS_LICENSE file in extracted source tarball

* Sun Mar 15 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.10-0.5
- Adapting to updated globus-core package

* Thu Feb 26 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.10-0.4
- Add s390x to the list of 64 bit platforms

* Thu Jan 01 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.10-0.3
- Adapt to updated GPT package

* Tue Oct 14 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.10-0.2
- Update to Globus Toolkit 4.2.1

* Mon Jul 14 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.4-0.1
- Autogenerated
