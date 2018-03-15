Name:		globus-xio-gsi-driver
%global soname 0
%global _name %(tr - _ <<< %{name})
Version:	4.1
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Globus XIO GSI Driver

Group:		System Environment/Libraries
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:	%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	globus-gssapi-error-devel >= 4
BuildRequires:	globus-gss-assist-devel >= 11
BuildRequires:	globus-xio-devel >= 3
BuildRequires:	globus-gssapi-gsi-devel >= 13
BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-xio-doc >= 3
BuildRequires:	doxygen
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:	automake >= 1.11
BuildRequires:	autoconf >= 2.60
BuildRequires:	libtool >= 2.2
%endif
BuildRequires:  pkgconfig

%if %{?suse_version}%{!?suse_version:0} >= 1315
%global mainpkg lib%{_name}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0} != 0
%package %{?nmainpkg}
Summary:	Grid Community Toolkit - Globus XIO GSI Driver
Group:		System Environment/Libraries
%endif

%package devel
Summary:	Grid Community Toolkit - Globus XIO GSI Driver Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
Requires:	globus-gssapi-error-devel%{?_isa} >= 4
Requires:	globus-gss-assist-devel%{?_isa} >= 11
Requires:	globus-xio-devel%{?_isa} >= 3
Requires:	globus-gssapi-gsi-devel%{?_isa} >= 13

%package doc
Summary:	Grid Community Toolkit - Globus XIO GSI Driver Documentation Files
Group:		Documentation
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6
BuildArch:	noarch
%endif
Requires:	%{mainpkg} = %{version}-%{release}

%if %{?suse_version}%{!?suse_version:0} >= 1315
%description %{?nmainpkg}
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{mainpkg} package contains:
Globus XIO GSI Driver
%endif

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Globus XIO GSI Driver

%description devel
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-devel package contains:
Globus XIO GSI Driver Development Files

%description doc
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-doc package contains:
Globus XIO GSI Driver Documentation Files

%prep
%setup -q -n %{_name}-%{version}

%build
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
# Remove files that should be replaced during bootstrap
rm -rf autom4te.cache

autoreconf -if
%endif

%configure \
           --disable-static \
           --docdir=%{_docdir}/%{name}-%{version} \
           --includedir=%{_includedir}/globus \
           --libexecdir=%{_datadir}/globus

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT

find $RPM_BUILD_ROOT%{_libdir} -name 'lib*.la' -exec rm -v '{}' \;

%post %{?nmainpkg} -p /sbin/ldconfig

%postun %{?nmainpkg} -p /sbin/ldconfig

%files %{?nmainpkg}
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}
%doc %{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%{_libdir}/libglobus*.so*

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*
%{_libdir}/pkgconfig/%{name}.pc

%files doc
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}/html
%{_docdir}/%{name}-%{version}/html/*
%{_mandir}/man3/*

%changelog
* Wed Sep 06 2017 Globus Toolkit <support@globus.org> - 4.1-1
- Add SNI and ALPN support via cntls
- Ignore error setting ALPN option

* Fri Apr 14 2017 Globus Toolkit <support@globus.org> - 3.11-1
- Fix crash when checking for anonymous GSS name when name comparison fails

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 3.10-3
- Rebuild after changes for el.5 with openssl101e

* Thu Aug 25 2016 Globus Toolkit <support@globus.org> - 3.10-2
- Updates for SLES 12

* Wed May 11 2016 Globus Toolkit <support@globus.org> - 3.10-1
- Fix anonymous auth in strict mode

* Tue Apr 05 2016 Globus Toolkit <support@globus.org> - 3.9-1
- Add dlpreopen variable to uninstalled pc file
- Propagate error back to caller when name mismatch occurs on server
  instead of just closing the handle

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 3.8-2
- Add vendor

* Thu Jul 23 2015 Globus Toolkit <support@globus.org> - 3.8-1
- GT-615: GSI XIO driver uses resolved IP address when importing names

* Thu May 28 2015 Globus Toolkit <support@globus.org> - 3.7-1
- Handle anonymous targets in GSI RFC2818 mode and document stringopts

* Thu Sep 25 2014 Globus Toolkit <support@globus.org> - 3.6-1
- Doxygen markup fixes
- Fix typos and clarify some documentation
- Quiet some autoconf/automake warnings

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 3.5-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 3.4-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 3.4-1
- Merge changes from Mattias Ellert

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 3.3-1
- Version bump for consistency

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 3.2-1
- Version bump for consistency

* Tue Feb 11 2014 Globus Toolkit <support@globus.org> - 3.1-1
- Packaging fixes

* Tue Jan 21 2014 Globus Toolkit <support@globus.org> - 3.0-1
- Repackage for GT6 without GPT

* Mon Oct 28 2013 Globus Toolkit <support@globus.org> - 2.4-1
- Remove reference to TCP as the underlying protocol.

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 2.3-9
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Wed Mar 06 2013 Globus Toolkit <support@globus.org> - 2.3-8
- missing globus-core build dependency

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 2.3-7
- Workaround missing F18 doxygen/latex dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 2.3-6
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 2.3-5
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 2.3-4
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 2.3-3
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 2.3-2
- SLES 11 patches

* Thu Mar 1 2012 Joseph Bester <bester@mcs.anl.gov> - 2.3-1
- RIC-239: GSSAPI Token inspection fails when using TLS 1.2

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 2.2-1
- RIC-224: Eliminate some doxygen warnings

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 2.1-4
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 2.1-3
- Last sync prior to 5.2.0

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 2.1-2
- Add explicit dependencies on >= 5.2 libraries

* Thu Oct 06 2011 Joseph Bester <bester@mcs.anl.gov> - 2.1-1
- Add backward-compatibility aging

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 2.0-2
- Update for 5.1.2 release

* Wed Apr 14 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-5
- Update to Globus Toolkit 5.0.1

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-4
- Update to Globus Toolkit 5.0.0

* Thu Jul 23 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-3
- Add instruction set architecture (isa) tags
- Make doc subpackage noarch

* Thu Jun 04 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-2
- Update to official Fedora Globus packaging guidelines

* Wed Apr 29 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-1
- Make comment about source retrieval more explicit
- Change defines to globals
- Remove explicit requires on library packages
- Put GLOBUS_LICENSE file in extracted source tarball
- Fix changed dependency namespace

* Sun Mar 15 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-0.5
- Adapting to updated globus-core package

* Thu Feb 26 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-0.4
- Add s390x to the list of 64 bit platforms

* Thu Jan 01 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-0.3
- Adapt to updated GPT package

* Tue Oct 21 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-0.2
- Update to Globus Toolkit 4.2.1

* Tue Jul 15 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.4-0.1
- Autogenerated
