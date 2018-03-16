Name:		globus-gridmap-callout
%global _name %(tr - _ <<< %{name})
Version:	1.1
Release:	3%{?dist}
Summary:	Grid Community Toolkit - Globus Gridmap Callout

Group:		System Environment/Libraries
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-gssapi-gsi-devel >= 4
BuildRequires:	globus-gss-assist-devel >= 3
BuildRequires:	globus-gridmap-callout-error-devel >= 2
BuildRequires:	globus-gsi-credential-devel >= 6
BuildRequires:	doxygen
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:	automake >= 1.11
BuildRequires:	autoconf >= 2.60
BuildRequires:	libtool >= 2.2
%endif
BuildRequires:	pkgconfig

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

# Remove libtool archives (.la files)
rm $RPM_BUILD_ROOT%{_libdir}/*.la

%post %{?nmainpkg} -p /sbin/ldconfig

%postun %{?nmainpkg} -p /sbin/ldconfig

%files %{?nmainpkg}
%defattr(-,root,root,-)
# This is a loadable module (plugin)
%{_libdir}/libglobus_gridmap_callout.so
%config(noreplace) %{_sysconfdir}/gridmap_callout-gsi_authz.conf
%dir %{_docdir}/%{name}-%{version}
%doc %{_docdir}/%{name}-%{version}/GLOBUS_LICENSE

%files doc
%defattr(-,root,root,-)
%doc %{_mandir}/man3/*
%dir %{_docdir}/%{name}-%{version}
%dir %{_docdir}/%{name}-%{version}/html
%doc %{_docdir}/%{name}-%{version}/html/*
%doc %{_docdir}/%{name}-%{version}/GLOBUS_LICENSE

%changelog
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
