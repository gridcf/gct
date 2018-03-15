Name:		globus-gridmap-verify-myproxy-callout
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	2.9
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Globus gridmap myproxy callout

Group:		System Environment/Libraries
License:	%{apache_license}
URL:		https://github.com/gridcf/gct/
Source:	%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

%if %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  openssl
BuildRequires:  libopenssl-devel
%else
BuildRequires:  openssl
BuildRequires:  openssl-devel
%endif

BuildRequires:	globus-gsi-sysconfig-devel >= 5
BuildRequires:	globus-gss-assist-devel >= 8
BuildRequires:	globus-gridmap-callout-error-devel
BuildRequires:	globus-gssapi-gsi-devel >= 9
BuildRequires:	globus-gsi-credential-devel >= 6
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  automake >= 1.11
BuildRequires:  autoconf >= 2.60
BuildRequires:  libtool >= 2.2
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
Summary:	Grid Community Toolkit - Globus gridmap myproxy callout
Group:		System Environment/Libraries
%endif

%if %{?suse_version}%{!?suse_version:0} >= 1315
%description %{?nmainpkg}
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{mainpkg} package contains:
Globus gridmap myproxy callout
%endif

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Globus gridmap myproxy callout

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
find $RPM_BUILD_ROOT%{_libdir} -name 'lib*.la' -exec rm -v '{}' \;

%check
make %{?_smp_mflags} check

%post %{?nmainpkg}
/sbin/ldconfig

%postun %{?nmainpkg} -p /sbin/ldconfig

%files %{?nmainpkg}
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}
%{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%config(noreplace) %{_sysconfdir}/gridmap_verify_myproxy_callout-gsi_authz.conf
%{_libdir}/libglobus_*

%changelog
* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 2.9-1
- Update for el.5 openssl101e

* Mon Aug 29 2016 Globus Toolkit <support@globus.org> - 2.8-3
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 2.8-1
- Update bug report URL

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 2.7-2
- Add vendor

* Wed Oct 08 2014 Globus Toolkit <support@globus.org> - 2.7-1
- GT-560: Verify sharing certs

* Tue Sep 30 2014 Globus Toolkit <support@globus.org> - 2.6-1
- Add missing dependencies
- Don't autoconf substitute conf file with no substitutions in it

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 2.5-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 2.4-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 2.4-1
- Merge changes from Mattias Ellert

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 2.3-1
- Version bump for consistency

* Thu Feb 27 2014 Globus Toolkit <support@globus.org> - 2.2-1
- Packaging fixes, Warning Cleanup

* Sat Feb 15 2014 Globus Toolkit <support@globus.org> - 2.1-1
- Packaging fixes

* Wed Jan 22 2014 Globus Toolkit <support@globus.org> - 2.0-1
- Repackage for GT6 without GPT

* Mon Oct 28 2013 Globus Toolkit <support@globus.org> - 1.5-1
- Update dependencies for new credential/assist functions

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 1.3-2
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Wed Jun 19 2013 Globus Toolkit <support@globus.org> - 1.3-1
- Add GLOBUS_OPENSSL in configure

* Tue Mar 19 2013 Globus Toolkit <support@globus.org> - 1.2-2
- Update sharing to support a full cert chain at logon

* Wed Mar 06 2013 Globus Toolkit <support@globus.org> - 1.2-1
- GT-341: GPT metadata problem in new myproxy callout package

* Tue Mar 05 2013 Globus Toolkit <support@globus.org> - 1.1-1
- GT-365: Switch sharing user identification from DN to CERT

* Mon Aug 13 2012 Joseph Bester <bester@mcs.anl.gov> - 0.1-1
- Autogenerated
