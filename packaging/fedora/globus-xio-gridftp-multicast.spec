Name:		globus-xio-gridftp-multicast

%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	1.7
Release:	4%{?dist}
Summary:	Globus Toolkit - Globus XIO GridFTP Multicast Driver

Group:		System Environment/Libraries
License:	%{apache_license}
URL:		http://toolkit.globus.org/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	globus-xio-devel >= 0
BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-ftp-client-devel >= 2
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  automake >= 1.11
BuildRequires:  autoconf >= 2.60
BuildRequires:  libtool >= 2.2
%endif
BuildRequires:  pkgconfig

%if %{?suse_version}%{!?suse_version:0} >= 1315
%global mainpkg libglobus_xio_gridftp_multicast_driver
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0} != 0
%package %{?nmainpkg}
Summary:	Globus Toolkit - Globus XIO GridFTP Multicast Driver
Group:		System Environment/Libraries
%endif

%package devel
Summary:	Globus Toolkit - Globus XIO GridFTP Multicast Driver Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
Requires:	globus-xio-devel%{?_isa} >= 0
Requires:	globus-common%{?_isa} >= 14

%if %{?suse_version}%{!?suse_version:0} >= 1315
%description %{?nmainpkg}
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{mainpkg} package contains:
Globus XIO GridFTP Multicast Driver
%endif

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
Globus XIO GridFTP Multicast Driver

%description devel
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-devel package contains:
Globus XIO GridFTP Multicast Driver Development Files

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
%{_libdir}/libglobus*so*

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*
%{_libdir}/pkgconfig/*.pc

%changelog
* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 1.7-4
- Rebuild after changes for el.5 with openssl101e

* Fri Aug 26 2016 Globus Toolkit <support@globus.org> - 1.7-3
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 1.7-1
- Update bug report URL

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 1.6-2
- Add vendor

* Tue Jul 14 2015 Globus Toolkit <support@globus.org> - 1.6-1
- Remove dead code
- Fix uninitialized variables
- Fix string parsing error

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 1.5-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 1.4-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 1.4-1
- Merge changes from Mattias Ellert

* Tue May 06 2014 Globus Toolkit <support@globus.org> - 1.3-1
- Don't version dynamic module

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 1.2-1
- Version bump for consistency

* Mon Feb 17 2014 Globus Toolkit <support@globus.org> - 1.1-1
- Packaging fixes

* Mon Feb 17 2014 Globus Toolkit <support@globus.org> - 1.0-2
- Packaging fixes

* Wed Jan 22 2014 Globus Toolkit <support@globus.org> - 1.0-1
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
