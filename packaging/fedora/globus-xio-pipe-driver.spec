Name:		globus-xio-pipe-driver
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	3.10
Release:	1%{?dist}
Summary:	Globus Toolkit - Globus Pipe Driver

Group:		System Environment/Libraries
License:	%{apache_license}
URL:		http://toolkit.globus.org/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	globus-xio-devel >= 3
BuildRequires:	globus-common-devel >= 14
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
Summary:	Globus Toolkit - Globus Pipe Driver
Group:		System Environment/Libraries
%endif

%package devel
Summary:	Globus Toolkit - Globus Pipe Driver Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
Requires:	globus-xio-devel%{?_isa} >= 3

%if %{?suse_version}%{!?suse_version:0} >= 1315
%description %{?nmainpkg}
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{mainpkg} package contains:
Globus Pipe Driver
%endif

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
Globus Pipe Driver

%description devel
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-devel package contains:
Globus Pipe Driver Development Files

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
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

find $RPM_BUILD_ROOT%{_libdir} -name 'lib*.la' -exec rm -vf "{}" \;

%clean
rm -rf $RPM_BUILD_ROOT

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
%{_libdir}/pkgconfig/*.pc

%changelog
* Tue Apr 04 2017 Globus Toolkit <support@globus.org> - 3.10-1
- Fix .pc typo

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 3.9-3
- Rebuild after changes for el.5 with openssl101e

* Thu Aug 25 2016 Globus Toolkit <support@globus.org> - 3.9-2
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 3.9-1
- Update bug report URL

* Tue Apr 05 2016 Globus Toolkit <support@globus.org> - 3.8-1
- Add dlpreopen variable to uninstalled pc file

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 3.7-3
- Add vendor

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 3.7-2
- put .so file in only one package

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 3.7-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 3.6-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 3.6-1
- Merge changes from Mattias Ellert

* Tue May 06 2014 Globus Toolkit <support@globus.org> - 3.5-1
- Don't version dynamic module

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 3.4-1
- Version bump for consistency

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 3.3-1
- Version bump for consistency

* Fri Feb 14 2014 Globus Toolkit <support@globus.org> - 3.2-1
- Packaging fixes

* Wed Feb 12 2014 Globus Toolkit <support@globus.org> - 3.1-1
- Packaging fixes

* Tue Jan 21 2014 Globus Toolkit <support@globus.org> - 3.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 2.2-7
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Tue Mar 05 2013 Globus Toolkit <support@globus.org> - 2.2-6
- Add missing build dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 2.2-5
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 2.2-4
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 2.2-3
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 2.2-2
- RHEL 4 patches

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 2.2-1
- RIC-226: Some dependencies are missing in GPT metadata
- RIC-229: Clean up GPT metadata

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

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.1-1
- Autogenerated
