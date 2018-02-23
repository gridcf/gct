Name:		globus-authz
%global soname 0
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	3.15
Release:	1%{?dist}
Summary:	Globus Toolkit - Globus authz library

Group:		System Environment/Libraries
License:	%{apache_license}
URL:		http://toolkit.globus.org/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	globus-authz-callout-error-devel >= 2
BuildRequires:	globus-callout-devel >= 2
BuildRequires:	globus-gssapi-gsi-devel >= 9
BuildRequires:	globus-common-devel >= 14
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
%if 0%{?suse_version} > 0
BuildRequires: libtool
%else
BuildRequires: libtool-ltdl-devel
%endif

%if %{?rhel}%{!?rhel:0} == 5
BuildRequires: openssl101e
%else
BuildRequires: openssl
%endif

%if %{?suse_version}%{!?suse_version:0} >= 1315
%global mainpkg lib%{_name}%{soname}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0} != 0
%package %{?nmainpkg}
Summary:	Globus Toolkit - Globus authz library
Group:		System Environment/Libraries
%endif

%package devel
Summary:	Globus Toolkit - Globus authz library Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
Requires:	globus-authz-callout-error-devel%{?_isa}
Requires:	globus-callout-devel%{?_isa}
Requires:	globus-gssapi-gsi-devel%{?_isa} >= 9

%package doc
Summary:	Globus Toolkit - Globus authz library Documentation Files
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
Globus authz library
%endif

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
Globus authz library

%description devel
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-devel package contains:
Globus authz library Development Files

%description doc
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-doc package contains:
Globus authz library Documentation Files

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

%check
make %{?_smp_mflags} check

%clean
rm -rf $RPM_BUILD_ROOT

%post %{?nmainpkg} -p /sbin/ldconfig

%postun %{?nmainpkg} -p /sbin/ldconfig

%files %{?nmainpkg}
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}
%doc %{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%{_libdir}/libglobus_*so.*

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*
%{_libdir}/libglobus_*.so
%{_libdir}/pkgconfig/%{name}.pc

%files doc
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}/html
%{_docdir}/%{name}-%{version}/html/*
%{_mandir}/*/*

%changelog
* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 3.15-1
- Update for el.5 openssl101e

* Thu Aug 25 2016 Globus Toolkit <support@globus.org> - 3.14-2
- Updates for SLES 12

* Thu Aug 18 2016 Globus Toolkit <support@globus.org> - 3.14-1
- Makefile fix

* Tue Aug 16 2016 Globus Toolkit <support@globus.org> - 3.13-1
- Updates for OpenSSL 1.1.0

* Wed Apr 06 2016 Globus Toolkit <support@globus.org> - 3.12-1
- Fix authz callout load in test on mac

* Fri Nov 20 2015 Globus Toolkit <support@globus.org> - 3.11-1
- Fix paths for mingw tests

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 3.10-2
- Add vendor

* Wed Sep 24 2014 Globus Toolkit <support@globus.org> - 3.10-1
- Doxygen markup fixes
- Include more manpages for API
- Fix typos and clarify some documentation

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 3.9-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 3.8-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 3.8-1
- Merge changes from Mattias Ellert

* Mon Apr 21 2014 Globus Toolkit <support@globus.org> - 3.7-1
- Test fixes

* Mon Apr 21 2014 Globus Toolkit <support@globus.org> - 3.6-1
- Test fixes

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 3.5-1
- Version bump for consistency

* Thu Feb 13 2014 Globus Toolkit <support@globus.org> - 3.4-1
- Test Fixes

* Thu Feb 13 2014 Globus Toolkit <support@globus.org> - 3.3-1
- Test fixes

* Thu Feb 13 2014 Globus Toolkit <support@globus.org> - 3.1-1
- Test fixes

* Tue Jan 21 2014 Globus Toolkit <support@globus.org> - 3.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 2.2-9
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Wed Mar 06 2013 Globus Toolkit <support@globus.org> - 2.2-8
- add missing build dependencies

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 2.2-7
- Workaround missing F18 doxygen/latex dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 2.2-6
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 2.2-5
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 2.2-4
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 2.2-3
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 2.2-2
- SLES 11 patches

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

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.7-2
- Update to Globus Toolkit 5.0.0

* Tue Jul 28 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.7-1
- Autogenerated
