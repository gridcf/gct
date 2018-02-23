Name:		globus-gram-client
%global soname 3
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	13.19
Release:	1%{?dist}
Summary:	Globus Toolkit - GRAM Client Library

Group:		System Environment/Libraries
License:	%{apache_license}
URL:		http://toolkit.globus.org/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	globus-gram-protocol-devel >= 11
BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-rsl-devel >= 9
BuildRequires:	globus-io-devel >= 9
BuildRequires:	globus-gram-protocol-doc >= 11
BuildRequires:	globus-common-doc >= 14
BuildRequires:	openssl
BuildRequires:	doxygen
BuildRequires:	graphviz
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  automake >= 1.11
BuildRequires:  autoconf >= 2.60
BuildRequires:  libtool >= 2.2
%endif
%if 0%{?suse_version} > 0
BuildRequires: libtool
%else
BuildRequires: libtool-ltdl-devel
%endif
BuildRequires:  pkgconfig
%if %{?fedora}%{!?fedora:0} >= 18 || %{?rhel}%{!?rhel:0} >= 6
BuildRequires:  perl-Test-Simple
%endif

%if %{?suse_version}%{!?suse_version:0} >= 1315
%global mainpkg lib%{_name}%{soname}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0} != 0
%package %{?nmainpkg}
Summary:	Globus Toolkit - GRAM Client Library
Group:		System Environment/Libraries
%endif

%package devel
Summary:	Globus Toolkit - GRAM Client Library Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
Requires:	globus-gram-protocol-devel%{?_isa} >= 11
Requires:	globus-common-devel%{?_isa} >= 14
Requires:	globus-rsl-devel%{?_isa} >= 9
Requires:	globus-io-devel%{?_isa} >= 9

%package doc
Summary:	Globus Toolkit - GRAM Client Library Documentation Files
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
GRAM Client Library
%endif

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
GRAM Client Library

%description devel
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-devel package contains:
GRAM Client Library Development Files

%description doc
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-doc package contains:
GRAM Client Library Documentation Files

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

# Remove libtool archives (.la files)
find $RPM_BUILD_ROOT%{_libdir} -name 'lib*.la' -exec rm -v '{}' \;

%check
GLOBUS_HOSTNAME=localhost make %{?_smp_mflags} check

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
%{_docdir}/%{name}-%{version}/html/*
%{_mandir}/man3/*

%changelog
* Tue Jul 11 2017 Globus Toolkit <support@globus.org> - 13.19-1
- Add option to disable tests which require network

* Fri Jun 23 2017 Globus Toolkit <support@globus.org> - 13.18-1
- Disable network-needing tests in pbuilder environment

* Thu Jun 22 2017 Globus Toolkit <support@globus.org> - 13.17-1
- Fix tests makefile when openssl not present
- Add rpm build dependency on openssl

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 13.16-1
- Update for el.5 openssl101e

* Mon Aug 29 2016 Globus Toolkit <support@globus.org> - 13.15-2
- Updates for SLES 12

* Thu Aug 18 2016 Globus Toolkit <support@globus.org> - 13.15-1
- Makefile fix

* Tue Aug 16 2016 Globus Toolkit <support@globus.org> - 13.14-1
- Updates for OpenSSL 1.1.0

* Mon Apr 18 2016 Globus Toolkit <support@globus.org> - 13.13-1
- Use prelinks for tests so that they run on El Capitan

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 13.12-2
- Add vendor

* Mon Jan 05 2015 Globus Toolkit <support@globus.org> - 13.12-1
- Fix test on static build

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 13.11-1
- doxygen fixes

* Wed Oct 22 2014 Globus Toolkit <support@globus.org> - 13.10-2
- GT-567: Remove requirement that GRAM5 use SSLv3
- Add libtool-ltdl-devel dependency

* Mon Sep 22 2014 Globus Toolkit <support@globus.org> - 13.9-1
- Include more manpages for API
- Fix some Doxygen issues
- Quiet some autoconf/automake warnings

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 13.8-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 13.7-3
- Fix Source path

* Wed Jul 23 2014 Globus Toolkit <support@globus.org> - 13.7-2
- Add %check to build

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 13.7-1
- Merge changes from Mattias Ellert

* Thu Apr 24 2014 Globus Toolkit <support@globus.org> - 13.6-1
- Test fixes

* Thu Apr 24 2014 Globus Toolkit <support@globus.org> - 13.5-1
- Packaging fixes

* Tue Apr 22 2014 Globus Toolkit <support@globus.org> - 13.4-1
- Test fixes

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 13.3-1
- Version bump for consistency

* Thu Feb 27 2014 Globus Toolkit <support@globus.org> - 13.2-1
- Packaging fixes, Warning Cleanup

* Wed Jan 22 2014 Globus Toolkit <support@globus.org> - 13.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 12.4-8
- GT-424: New Fedora Packaging Guideline - no %_isa in BuildRequires

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 12.4-7
- Workaround missing F18 doxygen/latex dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 12.4-6
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 12.4-5
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 12.4-4
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 12.4-3
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 12.4-2
- SLES 11 patches

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 12.4-1
- RIC-226: Some dependencies are missing in GPT metadata

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 12.3-3
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 12.3-2
- Last sync prior to 5.2.0

* Tue Nov 15 2011 Joseph Bester <bester@mcs.anl.gov> - 12.3-1
- Enable IPv6 support

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 12.2-2
- Add explicit dependencies on >= 5.2 libraries

* Thu Oct 06 2011 Joseph Bester <bester@mcs.anl.gov> - 12.2-1
- Add backward-compatibility aging

* Tue Sep 20 2011  <bester@mcs.anl.gov> - 12.1-1
- Use GLOBUS_IO_SECURE_CHANNEL_MODE_GSI_WRAP_SSL3 to force SSLv3 when sending a message to the gatekeeper

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 12.0-2
- Update for 5.1.2 release

* Sun Jun 05 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 10.4-4
- Fix doxygen markup

* Mon Apr 25 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 10.4-3
- Add README file

* Tue Feb 08 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 10.4-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_15_Mass_Rebuild

* Sat Jul 17 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 10.4-1
- Update to Globus Toolkit 5.0.2

* Wed Apr 14 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 10.3-1
- Update to Globus Toolkit 5.0.1
- Drop patch globus-gram-client-typo.patch (fixed upstream)

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 9.1-1
- Update to Globus Toolkit 5.0.0

* Tue Jul 28 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 7.2-1
- Autogenerated
