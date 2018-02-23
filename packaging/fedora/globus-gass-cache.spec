Name:		globus-gass-cache
%global soname 5
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	9.10
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Globus Gass Cache

Group:		System Environment/Libraries
License:	%{apache_license}
URL:		https://github.com/gridcf/gct/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

%if %{?rhel}%{!?rhel:0} == 5
Requires:	openssl101e
%else
Requires:	openssl
%endif
Requires:	globus-common%{?_isa} >= 14

BuildRequires:	globus-common-devel >= 14
%if %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  openssl
BuildRequires:  libopenssl-devel
%else
%if %{?rhel}%{!?rhel:0} == 5
BuildRequires:  openssl101e
BuildRequires:  openssl101e-devel
BuildConflicts: openssl-devel
%else
BuildRequires:  openssl
BuildRequires:  openssl-devel
%endif
%endif
BuildRequires:	doxygen
BuildRequires:	graphviz
%if "%{?rhel}" == "5"
BuildRequires:	graphviz-gd
%endif
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  automake >= 1.11
BuildRequires:  autoconf >= 2.60
BuildRequires:  libtool >= 2.2
%endif
BuildRequires:  pkgconfig

%if %{?suse_version}%{!?suse_version:0} >= 1315
%global mainpkg lib%{_name}%{soname}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0} != 0
%package %{?nmainpkg}
Summary:	Grid Community Toolkit - Globus Gass Cache
Group:		System Environment/Libraries
%endif

%package devel
Summary:	Grid Community Toolkit - Globus Gass Cache Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
Requires:	globus-common-devel%{?_isa} >= 14
%if %{?suse_version}%{!?suse_version:0} >= 1315
Requires:  libopenssl-devel
%else
%if %{?rhel}%{!?rhel:0} == 5
Requires:  openssl101e-devel
%else
Requires:  openssl-devel
%endif
%endif

%package doc
Summary:	Grid Community Toolkit - Globus Gass Cache Documentation Files
Group:		Documentation
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6
BuildArch:	noarch
%endif
Requires:	%{mainpkg} = %{version}-%{release}

%if %{?suse_version}%{!?suse_version:0} >= 1315
%description %{?nmainpkg}
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{mainpkg} package contains:
Globus Gass Cache
%endif

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
Globus Gass Cache

%description devel
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-devel package contains:
Globus Gass Cache Development Files

%description doc
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-doc package contains:
Globus Gass Cache Documentation Files

%prep
%setup -q -n %{_name}-%{version}

%build
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
# Remove files that should be replaced during bootstrap
rm -rf autom4te.cache

autoreconf -if
%endif

%if %{?rhel}%{!?rhel:0} == 5
export OPENSSL="$(which openssl101e)"
%endif

%configure \
           --disable-static \
           --docdir=%{_docdir}/%{name}-%{version} \
           --includedir=%{_includedir}/globus \
           --libexecdir=%{_datadir}/globus

make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

# Remove libtool archives (.la files)
find $RPM_BUILD_ROOT%{_libdir} -name 'lib*.la' -exec rm -v '{}' \;

%clean
rm -rf $RPM_BUILD_ROOT

%post %{?nmainpkg} -p /sbin/ldconfig

%postun %{?nmainpkg} -p /sbin/ldconfig

%files %{?nmainpkg}
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}
%{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%{_libdir}/libglobus*.so.*

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*
%{_libdir}/libglobus*.so
%{_libdir}/pkgconfig/*.pc

%files doc
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}/html
%{_datadir}/man/man3/*
%{_docdir}/%{name}-%{version}/html/*

%changelog
* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 9.10-1
- Update for el.5 openssl101e

* Fri Aug 26 2016 Globus Toolkit <support@globus.org> - 9.9-2
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 9.9-1
- Update bug report URL

* Tue May 03 2016 Globus Toolkit <support@globus.org> - 9.8-1
- Spelling

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 9.7-2
- Add vendor

* Tue Jul 28 2015 Globus Toolkit <support@globus.org> - 9.7-1
- regression: bad error check

* Tue Jul 28 2015 Globus Toolkit <support@globus.org> - 9.6-1
- GT-618: GASS Cache error mishandling causes crash

* Mon Sep 22 2014 Globus Toolkit <support@globus.org> - 9.5-1
- Include more manpages for API
- Fix some Doxygen issues
- Quiet some autoconf/automake warnings

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 9.4-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 9.3-3
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 9.3-1
- Merge changes from Mattias Ellert

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 9.2-1
- Version bump for consistency

* Tue Feb 25 2014 Globus Toolkit <support@globus.org> - 9.1-1
- Packaging fixes

* Wed Jan 22 2014 Globus Toolkit <support@globus.org> - 9.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 8.1-10
- GT-424: New Fedora Packaging Guideline - no %_isa in BuildRequires

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 8.1-9
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 8.1-8
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 8.1-7
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 8.1-6
- RHEL 4 patches

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 8.1-5
- Updated version numbers

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 8.1-4
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 8.1-3
- Last sync prior to 5.2.0

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 8.1-2
- Add explicit dependencies on >= 5.2 libraries
- GRAM-221: GASS Cache uses sprintf %n

* Thu Oct 06 2011 Joseph Bester <bester@mcs.anl.gov> - 8.1-1
- Add backward-compatibility aging

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 8.0-2
- Update for 5.1.2 release

* Mon Apr 25 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.4-4
- Add README file

* Tue Feb 08 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 5.4-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_15_Mass_Rebuild

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.4-2
- Update to Globus Toolkit 5.0.0

* Thu Jul 30 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.4-1
- Autogenerated
