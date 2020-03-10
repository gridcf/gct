%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-gridmap-callout
%global _name %(echo %{name} | tr - _)
Version:	2.2
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Globus Gridmap Callout

Group:		System Environment/Libraries
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	gcc
BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-gssapi-gsi-devel >= 4
BuildRequires:	globus-gss-assist-devel >= 3
BuildRequires:	globus-gridmap-callout-error-devel >= 2
BuildRequires:	globus-gsi-credential-devel >= 6
BuildRequires:	doxygen

%if %{?suse_version}%{!?suse_version:0}
%global mainpkg lib%{_name}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0}
%package %{?nmainpkg}
Summary:	Grid Community Toolkit - Globus Gridmap Callout
Group:		System Environment/Libraries
Provides:	%{name} = %{version}-%{release}
Obsoletes:	%{name} < %{version}-%{release}
%endif

%package doc
Summary:	Grid Community Toolkit - Globus Gridmap Callout Documentation Files
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
Globus Gridmap Callout
%endif

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Globus Gridmap Callout

%description doc
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-doc package contains:
Globus Gridmap Callout Documentation Files

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
%{_libdir}/libglobus_gridmap_callout.so
%config(noreplace) %{_sysconfdir}/gridmap_callout-gsi_authz.conf
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%files doc
%defattr(-,root,root,-)
%doc %{_mandir}/man3/*
%dir %{_pkgdocdir}
%dir %{_pkgdocdir}/html
%doc %{_pkgdocdir}/html/*
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%changelog
* Tue Mar 10 2020 Mattias Ellert <mattias.ellert@physics.uu.se> - 2.2-1
- Make makefiles exit sooner on errors

* Wed Nov 21 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 2.1-1
- Doxygen fixes

* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 2.0-1
- First Grid Community Toolkit release

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 1.1-3
- Rebuild after changes for el.5 with openssl101e

* Mon Aug 29 2016 Globus Toolkit <support@globus.org> - 1.1-2
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 1.1-1
- Update bug report URL

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 1.0-2
- Add vendor

* Thu Sep 25 2014 Globus Toolkit <support@globus.org> - 1.0-1
- Add package for GT6
