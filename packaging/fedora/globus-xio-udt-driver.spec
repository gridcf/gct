%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-xio-udt-driver
%global _name %(tr - _ <<< %{name})
Version:	2.0
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Globus XIO UDT Driver

Group:		System Environment/Libraries
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	gcc
BuildRequires:	gcc-c++
BuildRequires:	globus-xio-devel >= 3
BuildRequires:	globus-common-devel >= 14
BuildRequires:	libnice-devel
BuildRequires:	udt-devel

%if %{?suse_version}%{!?suse_version:0}
%global mainpkg lib%{_name}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0}
%package %{?nmainpkg}
Summary:	Grid Community Toolkit - Globus XIO UDT Driver
Group:		System Environment/Libraries
Provides:	%{name} = %{version}-%{release}
Obsoletes:	%{name} < %{version}-%{release}
%endif

%package devel
Summary:	Grid Community Toolkit - Globus XIO UDT Driver Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}

%if %{?nmainpkg:1}%{!?nmainpkg:0}
%description %{?nmainpkg}
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{mainpkg} package contains:
Globus XIO UDT Driver
%endif

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Globus XIO UDT Driver

%description devel
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-devel package contains:
Globus XIO UDT Driver Development Files

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
# This is a loadable module (plugin)
%{_libdir}/libglobus_xio_udt_driver.so
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*
%{_libdir}/pkgconfig/%{name}.pc

%changelog
* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 2.0-1
- First Grid Community Toolkit release

* Thu Jan 25 2018 Globus Toolkit <support@globus.org> - 1.29-1
- update gettext for win build

* Mon Jun 26 2017 Globus Toolkit <support@globus.org> - 1.28-1
- Fix Glib build

* Tue Apr 25 2017 Globus Toolkit <support@globus.org> - 1.27-1
- Don't force static build

* Wed Dec 21 2016 Globus Toolkit <support@globus.org> - 1.26-1
- Fix build failure on mingw with gcc 5.4.0

* Wed Oct 05 2016 Globus Toolkit <support@globus.org> - 1.25-2
- Add libselinux-devel dependency for SLES 12

* Wed Oct 05 2016 Globus Toolkit <support@globus.org> - 1.25-1
- pull udt tarball from globus repo

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 1.24-7
- Rebuild after changes for el.5 with openssl101e

* Thu Aug 25 2016 Globus Toolkit <support@globus.org> - 1.24-5
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 1.24-1
- Update bug report URL

* Thu Jun 02 2016 Globus Toolkit <support@globus.org> - 1.23-3
- More feature tests for libnice
- BuildRequires for libnice/glib for el.5
- Fix Requires for libnice/glib for el.5

* Thu Jun 02 2016 Globus Toolkit <support@globus.org> - 1.22-2
- Having packaged libnice from el.6 for el.5, and update dependencies

* Thu Jun 02 2016 Globus Toolkit <support@globus.org> - 1.22-1
- Allow building using the RHEL 6 version of libnice

* Wed May 25 2016 Globus Toolkit <support@globus.org> - 1.21-1
- add GLOBUS_XIO_UDT_STUNSERVER env override

* Wed Apr 27 2016 Globus Toolkit <support@globus.org> - 1.20-1
- Don't configure glib2 during unpack

* Mon Sep 21 2015 Globus Toolkit <support@globus.org> - 1.19-1
- ignore other end's attempts at ipv6 negotiation

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 1.18-2
- Add vendor

* Thu Jul 23 2015 Globus Toolkit <support@globus.org> - 1.18-1
- don't attempt ice negotiation over ipv6 while udt driver does not support ipv6

* Mon Jun 15 2015 Globus Toolkit <support@globus.org> - 1.17-1
- Fix error checking and automake warning

* Tue May 19 2015 Globus Toolkit <support@globus.org> - 1.16-4
- Fedora 22 needs libselinux-devel

* Fri Mar 06 2015 Globus Toolkit <support@globus.org> - 1.16-3
- SLES 11 needs libffi43

* Wed Dec 17 2014 Globus Toolkit <support@globus.org> - 1.16-2
- Dependency on gupnp-igd-devel for Fedora 21

* Thu Oct 30 2014 Globus Toolkit <support@globus.org> - 1.16-1
- Add support for debian squeeze and ubuntu lucid

* Wed Oct 29 2014 Globus Toolkit <support@globus.org> - 1.15-2
- Use native libs for EL7

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 1.15-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 1.14-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 1.14-1
- Merge changes from Mattias Ellert

* Sat Apr 26 2014 Globus Toolkit <support@globus.org> - 1.13-1
- Packaging fixes

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 1.12-1
- Packaging fixes

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 1.11-1
- Packaging fixes

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 1.10-1
- Packaging fixes

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 1.9-1
- Packaging fixes

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 1.8-1
- Packaging fixes

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 1.7-1
- Packaging fixes

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 1.6-1
- Packaging fixes

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 1.5-1
- Version bump for consistency

* Wed Mar 05 2014 Globus Toolkit <support@globus.org> - 1.0-1
- Packaging fixes

* Wed Oct 16 2013 Globus Toolkit <support@globus.org> - 0.6-2
- New package
