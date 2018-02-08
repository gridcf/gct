Name:		globus-xio-udt-driver
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	1.29
Release:	1%{?dist}
Vendor:	Globus Support
Summary:	Globus Toolkit - Globus XIO UDT Driver

Group:		System Environment/Libraries
License:	%{apache_license}
URL:		http://toolkit.globus.org/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	gcc-c++

%if %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:	libudt
BuildRequires:	udt-devel
%else
BuildRequires:	udt
BuildRequires:	udt-devel
%endif

BuildRequires:	globus-xio-devel >= 3
BuildRequires:	globus-common-devel >= 14
%if %{?fedora}%{!?fedora:0} >= 18
BuildRequires:  glib2-devel >= 2.32
BuildRequires:  libnice-devel >= 0.0.12
%else
%if %{?rhel}%{!?rhel:0} >= 5 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:       glib2-devel%{?_isa} >= 2.12
BuildRequires:       libnice-devel%{?_isa} >= 0.0.9
%endif
%if 0%{?suse_version} > 0
BuildRequires:  gettext-tools
%else
BuildRequires:  gettext-devel
%endif
BuildRequires:  xz
BuildRequires:  curl
BuildRequires:  zlib-devel
%endif
%if %{?rhel}%{!?rhel:0} == 5
BuildRequires:  python26
%endif
BuildRequires:  libffi-devel
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  automake >= 1.11
BuildRequires:  autoconf >= 2.60
BuildRequires:  libtool >= 2.2
%endif
BuildRequires:  pkgconfig
%if %{?fedora}%{!?fedora:0} >= 21
BuildRequires:  gupnp-igd-devel
%endif
%if %{?fedora}%{!?fedora:0} >= 22 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires: libselinux-devel
%endif

%if %{?suse_version}%{!?suse_version:0} >= 1315
%global mainpkg lib%{_name}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0} != 0
%package %{?nmainpkg}
Summary:	Globus Toolkit - Globus XIO UDT Driver
Group:		System Environment/Libraries
%endif

%package devel
Summary:	Globus Toolkit - Globus XIO UDT Driver Development Files
Group:		Development/Libraries
Requires:	%{name}%{?_isa} = %{version}-%{release}
Requires:	globus-xio-devel%{?_isa} >= 3

%if %{?suse_version}%{!?suse_version:0} >= 1315
%description %{?nmainpkg}
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{mainpkg} package contains:
Globus XIO UDT Driver
%endif

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
Globus XIO UDT Driver

%description devel
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-devel package contains:
Globus XIO UDT Driver Development Files

%prep
%setup -q -n %{_name}-%{version}

%build
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
# Remove files that should be replaced during bootstrap
rm -rf autom4te.cache

autoreconf -if
%endif


%if 0%{?suse_version} > 0 && %{?suse_version}%{!?suse_version:0} < 1315
# SuSE 11 doesn't include libffi's pkg-config file, but the library
# is available natively. LIBFFI_CFLAGS must be non-empty for autoconf to
# detect it as set in the configure invocation in the glib2 source directory
export LIBFFI_CFLAGS="-DGT6_UDT_DRIVER_SuSE_HACK"
export LIBFFI_LIBS="-lffi"
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

find $RPM_BUILD_ROOT%{_libdir} -name 'lib*.la' -exec rm -v '{}' \;

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
%{_libdir}/pkgconfig/%{name}.pc

%changelog
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
