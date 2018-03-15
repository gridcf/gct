Name:		globus-io
%global soname 3
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	11.9
Release:	1%{?dist}
Summary:	Grid Community Toolkit - uniform I/O interface

Group:		System Environment/Libraries
License:	%{apache_license}
URL:		https://github.com/gridcf/gct/
Source:	%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

%if %{?suse_version}%{!?suse_version:0} >= 1315
Requires:	libglobus_xio_gsi_driver%{?_isa} >= 2
%else
Requires:	globus-xio-gsi-driver%{?_isa} >= 2
%endif

BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-xio-gsi-driver-devel >= 2
BuildRequires:	globus-gss-assist-devel >= 8
BuildRequires:	globus-xio-devel >= 3
BuildRequires:	globus-gssapi-gsi-devel >= 10
BuildRequires:	globus-gssapi-error-devel >= 4
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:	automake >= 1.11
BuildRequires:	autoconf >= 2.60
BuildRequires:	libtool >= 2.2
%endif
BuildRequires:  pkgconfig
%if %{?fedora}%{!?fedora:0} >= 18 || %{?rhel}%{!?rhel:0} >= 6
BuildRequires:  perl-Test-Simple
%endif
%if 0%{?suse_version} > 0
BuildRequires: libtool
%else
BuildRequires: libtool-ltdl-devel
%endif
BuildRequires:  openssl

%if %{?suse_version}%{!?suse_version:0} >= 1315
%global mainpkg lib%{_name}%{soname}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0} != 0
%package %{?nmainpkg}
Summary:	Grid Community Toolkit - uniform I/O interface
Group:		System Environment/Libraries
%endif

%package devel
Summary:	Grid Community Toolkit - uniform I/O interface Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
Requires:	globus-common-devel%{?_isa} >= 14
Requires:	globus-xio-gsi-driver-devel%{?_isa} >= 2
Requires:	globus-gss-assist-devel%{?_isa} >= 8
Requires:	globus-xio-devel%{?_isa} >= 3
Requires:	globus-gssapi-gsi-devel%{?_isa} >= 10
Requires:	globus-gssapi-error-devel%{?_isa} >= 4

%if %{?suse_version}%{!?suse_version:0} >= 1315
%description %{?nmainpkg}
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{mainpkg} package contains:
uniform I/O interface to stream and datagram style communications
%endif

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
uniform I/O interface to stream and datagram style communications

%description devel
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-devel package contains:
uniform I/O interface Development Files

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
rm -rvf $RPM_BUILD_ROOT%{_mandir}

%check
GLOBUS_HOSTNAME=localhost make %{?_smp_mflags} check

%post %{?nmainpkg} -p /sbin/ldconfig

%postun %{?nmainpkg} -p /sbin/ldconfig

%files %{?nmainpkg}
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}
%doc %{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%{_libdir}/libglobus_*.so.*

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*
%{_libdir}/lib*.so
%{_libdir}/pkgconfig/%{name}.pc

%changelog
* Fri Apr 21 2017 Globus Toolkit <support@globus.org> - 11.9-1
- Remove legacy SSLv3 support

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 11.8-1
- Update for el.5 openssl101e

* Thu Aug 25 2016 Globus Toolkit <support@globus.org> - 11.7-3
- Updates for SLES 12

* Thu Aug 18 2016 Globus Toolkit <support@globus.org> - 11.7-1
- Makefile fix

* Tue Aug 16 2016 Globus Toolkit <support@globus.org> - 11.6-1
- Updates for OpenSSL 1.1.0

* Thu Apr 07 2016 Globus Toolkit <support@globus.org> - 11.5-1
- Use new dlpreopen variable from gsi driver to build tests for installer
- Fix uninitialized variable reads and some warnings in io tests

* Tue Dec 15 2015 Globus Toolkit <support@globus.org> - 11.4-3
- Add build dependency on openssl

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 11.4-2
- Add vendor

* Thu May 28 2015 Globus Toolkit <support@globus.org> - 11.4-1
- Improve test diagnostic messages

* Fri Jan 09 2015 Globus Toolkit <support@globus.org> - 11.3-1
- Better fix for testing on localhost

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 11.2-1
- Use localhost for tests

* Tue Oct 28 2014 Globus Toolkit <support@globus.org> - 11.1-1
- GT-477: Tracking TCP retransmits on the GridFTP server

* Thu Sep 25 2014 Globus Toolkit <support@globus.org> - 10.12-1
- Drop empty documentation from globus_io package
- Quiet some autoconf/automake warnings

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 10.11-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 10.10-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 10.10-1
- Merge changes from Mattias Ellert

* Mon Apr 21 2014 Globus Toolkit <support@globus.org> - 10.9-1
- Test fixes

* Mon Apr 21 2014 Globus Toolkit <support@globus.org> - 10.8-1
- Test fixes

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 10.7-1
- Version bump for consistency

* Mon Mar 03 2014 Globus Toolkit <support@globus.org> - 10.5-1
- Packaging fixes

* Mon Feb 24 2014 Globus Toolkit <support@globus.org> - 10.4-1
- Test fixes

* Mon Feb 24 2014 Globus Toolkit <support@globus.org> - 10.3-1
- Test fixes

* Tue Feb 11 2014 Globus Toolkit <support@globus.org> - 10.2-1
- Packaging fixes

* Sat Feb 01 2014 Globus Toolkit <support@globus.org> - 10.1-1
- umask for test creds

* Tue Jan 21 2014 Globus Toolkit <support@globus.org> - 10.0-1
- Repackage for GT6 without GPT

* Tue Oct 15 2013 Globus Toolkit <support@globus.org> - 9.5-1
- GT-470: Globus IO reports timeout error as cancellation

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 9.4-3
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Wed Mar 06 2013 Globus Toolkit <support@globus.org> - 9.4-2
- missing dependency on globus-gssapi-error

* Mon Feb 04 2013 Globus Toolkit <support@globus.org> - 9.4-1
- GT-32: Force IPv6 in globus_io with an environment variable

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 9.3-5
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 9.3-4
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 9.3-3
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 9.3-2
- RHEL 4 patches

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 9.3-1
- RIC-226: Some dependencies are missing in GPT metadata

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 9.2-3
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 9.2-2
- Last sync prior to 5.2.0

* Thu Dec 01 2011 Joseph Bester <bester@mcs.anl.gov> - 9.2-1
- GRAM-290: GRAM protocol misinterprets some GSSAPI errors as connection errors

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 9.1-2
- Add explicit dependencies on >= 5.2 libraries

* Thu Oct 06 2011 Joseph Bester <bester@mcs.anl.gov> - 9.1-1
- Add backward-compatibility aging

* Mon Sep 26 2011 Joseph Bester <bester@mcs.anl.gov> - 9.0-2
- pick up new GSSAPI version dependency

* Tue Sep 20 2011  <bester@mcs.anl.gov> - 9.0-1
- Add channel mode GLOBUS_IO_SECURE_CHANNEL_MODE_GSI_WRAP_SSL3 to force SSLv3

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 8.0-2
- Update for 5.1.2 release

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 6.3-4
- Update to Globus Toolkit 5.0.0

* Thu Jul 23 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 6.3-3
- Add instruction set architecture (isa) tags

* Thu Jun 04 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 6.3-2
- Update to official Fedora Globus packaging guidelines

* Thu Apr 16 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 6.3-1
- Make comment about source retrieval more explicit
- Change defines to globals
- Remove explicit requires on library packages
- Put GLOBUS_LICENSE file in extracted source tarball

* Sun Mar 15 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 6.3-0.5
- Adapting to updated globus-core package

* Thu Feb 26 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 6.3-0.4
- Add s390x to the list of 64 bit platforms

* Tue Dec 30 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 6.3-0.3
- Adapt to updated GPT package

* Tue Oct 21 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 6.3-0.2
- Update to Globus Toolkit 4.2.1

* Mon Jul 14 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 6.3-0.1
- Autogenerated
