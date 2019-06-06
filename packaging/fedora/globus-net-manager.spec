%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-net-manager
%global soname 0
%global _name %(echo %{name} | tr - _)
Version:	1.4
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Network Manager Library

Group:		System Environment/Libraries
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	gcc
BuildRequires:	globus-common-devel >= 15.27
BuildRequires:	globus-xio-devel >= 5
BuildRequires:	doxygen
%if %{?fedora}%{!?fedora:0} >= 30 || %{?rhel}%{!?rhel:0} >= 8
BuildRequires:	python3-devel
%else
BuildRequires:	python2-devel
%endif

%if %{?suse_version}%{!?suse_version:0}
%global mainpkg lib%{_name}%{soname}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?suse_version}%{!?suse_version:0}
%global driver_package libglobus_xio_net_manager_driver
%else
%global driver_package globus-xio-net-manager-driver
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0}
%package %{?nmainpkg}
Summary:	Grid Community Toolkit - Network Manager Library
Group:		System Environment/Libraries
Provides:	%{name} = %{version}-%{release}
Obsoletes:	%{name} < %{version}-%{release}
%endif

Requires:	globus-common%{?_isa} >= 15.27

%package devel
Summary:	Grid Community Toolkit - Network Manager Library Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}

%package -n %{driver_package}
Summary:	Grid Community Toolkit - Globus XIO Network Manager Driver
Group:		System Environment/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
Requires:	globus-xio%{?_isa} >= 5
Provides:	globus-net-manager-xio-driver = %{version}-%{release}
Obsoletes:	globus-net-manager-xio-driver < %{version}-%{release}
%if %{?suse_version}%{!?suse_version:0}
Provides:	globus-xio-net-manager-driver = %{version}-%{release}
Obsoletes:	globus-xio-net-manager-driver < %{version}-%{release}
%endif

%package -n globus-xio-net-manager-driver-devel
Summary:	Grid Community Toolkit - Globus XIO Network Manager Driver Development Files
Group:		Development/Libraries
Requires:	globus-xio-net-manager-driver%{?_isa} = %{version}-%{release}
Requires:	%{name}-devel%{?_isa} = %{version}-%{release}

%package doc
Summary:	Grid Community Toolkit - Network Manager Library Documentation Files
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
Network Manager Library
%endif

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Network Manager Library

%description devel
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-devel package contains:
Network Manager Library Development Files

%description -n %{driver_package}
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{driver_package} package contains:
Globus XIO Network Manager Driver

%description -n globus-xio-net-manager-driver-devel
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The globus-xio-net-manager-driver-devel package contains:
Globus XIO Network Manager Driver Development Files

%description doc
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-doc package contains:
Network Manager Library Documentation Files

%prep
%setup -q -n %{_name}-%{version}

%build
%if %{?fedora}%{!?fedora:0} >= 30 || %{?rhel}%{!?rhel:0} >= 8
export PYTHON_CONFIG=%{__python3}-config
%else
export PYTHON_CONFIG=%{__python2}-config
%endif

%configure --disable-static \
	   --includedir=%{_includedir}/globus \
	   --libexecdir=%{_datadir}/globus \
	   --docdir=%{_pkgdocdir} \
	   --enable-python

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT

# Remove libtool archives (.la files)
rm $RPM_BUILD_ROOT%{_libdir}/*.la

%check
GLOBUS_HOSTNAME=localhost make %{?_smp_mflags} check VERBOSE=1

%post %{?nmainpkg} -p /sbin/ldconfig

%postun %{?nmainpkg} -p /sbin/ldconfig

%post -n %{driver_package} -p /sbin/ldconfig

%postun -n %{driver_package} -p /sbin/ldconfig

%files %{?nmainpkg}
%defattr(-,root,root,-)
%{_libdir}/libglobus_net_manager.so.*
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/globus_net_manager.h
%{_includedir}/globus/globus_net_manager_attr.h
%{_libdir}/libglobus_net_manager.so
%{_libdir}/pkgconfig/%{name}.pc

%files -n %{driver_package}
%defattr(-,root,root,-)
# This is a loadable module (plugin)
%{_libdir}/libglobus_xio_net_manager_driver.so

%files -n globus-xio-net-manager-driver-devel
%defattr(-,root,root,-)
%{_includedir}/globus/globus_xio_net_manager_driver.h
%{_libdir}/pkgconfig/globus-xio-net-manager-driver.pc

%files doc
%defattr(-,root,root,-)
%doc %{_mandir}/man3/*
%dir %{_pkgdocdir}
%dir %{_pkgdocdir}/html
%doc %{_pkgdocdir}/html/*
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%changelog
* Wed Jun 05 2019 Mattias Ellert <mattias.ellert@physics.uu.se> - 1.4-1
- Check python-config for --embed flag (python 3.8 compatibility)

* Sat Nov 24 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 1.3-1
- Python 3 support
- Build using Python 3 for Fedora 30+
- Enable checks

* Wed Nov 21 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 1.2-1
- Doxygen fixes

* Tue May 01 2018 Globus Toolkit <support@globus.org> - 1.1-1
- fix pre-connect not using changed remote contact

* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 1.0-1
- First Grid Community Toolkit release
- Split out devel package for xio driver

* Tue Apr 04 2017 Globus Toolkit <support@globus.org> - 0.17-1
- Fix .pc typo

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 0.16-2
- Rebuild after changes for el.5 with openssl101e

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 0.16-1
- exclude tests from doc

* Fri Aug 26 2016 Globus Toolkit <support@globus.org> - 0.15-5
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 0.15-2
- Update bug report URL

* Mon Apr 18 2016 Globus Toolkit <support@globus.org> - 0.15-1
- Use prelinks for tests so that they run on El Capitan

* Fri Dec 18 2015 Globus Toolkit <support@globus.org> - 0.14-1
- pre_connect return attrs get set on attr, not handle

* Thu Oct 29 2015 Globus Toolkit <support@globus.org> - 0.13-1
- Remove unused code

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 0.12-2
- Add vendor

* Tue Jul 14 2015 Globus Toolkit <support@globus.org> - 0.12-1
- Fix linkage on Mac with libtool 2.4.6

* Tue Jul 14 2015 Globus Toolkit <support@globus.org> - 0.11-1
- Fix memory leaks, NULL pointer derefs, and dead assignments

* Wed Jul 01 2015 Globus Toolkit <support@globus.org> - 0.10-1
- Fix uninitialized value
- Remove unused variables

* Wed Jun 17 2015 Globus Toolkit <support@globus.org> - 0.9-1
- Fix missing documentation
- Clarify python invocation
- Fix error handling
- Add test for end_listen in python
- Allow running tests with valgrind

* Mon Jun 01 2015 Globus Toolkit <support@globus.org> - 0.8-2
- Rename xio driver package

* Mon Apr 13 2015 Globus Toolkit <support@globus.org> - 0.8-1
- fix for attr not being used on connect()

* Fri Mar 27 2015 Globus Toolkit <support@globus.org> - 0.7-1
- add file paramter to logging driver to set a file to log to.  use manager=logging;file=/path/to/file;.

* Fri Jan 09 2015 Globus Toolkit <support@globus.org> - 0.6-1
- Fix conflicts with globus-common-doc and globus-xio-doc

* Thu Jan 08 2015 Globus Toolkit <support@globus.org> - 0.5-1
- Fix test link on recent debians

* Wed Jan 07 2015 Globus Toolkit <support@globus.org> - 0.4-1
- Link in ltdl for tests

* Mon Jan 05 2015 Globus Toolkit <support@globus.org> - 0.3-1
- Tests run with static build

* Mon Dec 22 2014 Globus Toolkit <support@globus.org> - 0.2-1
- Fix missing skip test

* Fri Dec 19 2014 Globus Toolkit <support@globus.org> - 0.1-1
- check for python2.6-config

* Wed Dec 17 2014 Globus Toolkit <support@globus.org> - 0.0-1
- Initial package
