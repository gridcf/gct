%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-gridftp-server-control
%global soname 0
%global _name %(tr - _ <<< %{name})
Version:	7.0
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Globus GridFTP Server Library

Group:		System Environment/Libraries
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-xio-devel >= 3
BuildRequires:	globus-xio-gsi-driver-devel >= 2
BuildRequires:	globus-xio-pipe-driver-devel >= 2
BuildRequires:	globus-gss-assist-devel >= 8
BuildRequires:	globus-gssapi-gsi-devel >= 10
BuildRequires:	globus-gsi-openssl-error-devel >= 2
BuildRequires:	globus-gssapi-error-devel >= 4

%if %{?suse_version}%{!?suse_version:0}
%global mainpkg lib%{_name}%{soname}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0}
%package %{?nmainpkg}
Summary:	Grid Community Toolkit - Globus GridFTP Server Library
Group:		System Environment/Libraries
Provides:	%{name} = %{version}-%{release}
Obsoletes:	%{name} < %{version}-%{release}
%endif

Requires:	globus-xio-gsi-driver%{?_isa} >= 2
Requires:	globus-xio-pipe-driver%{?_isa} >= 2

%package devel
Summary:	Grid Community Toolkit - Globus GridFTP Server Library Development Files
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
Globus GridFTP Server Library
%endif

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Globus GridFTP Server Library

%description devel
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-devel package contains:
Globus GridFTP Server Library Development Files

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
%{_libdir}/libglobus_gridftp_server_control.so.*
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*
%{_libdir}/libglobus_gridftp_server_control.so
%{_libdir}/pkgconfig/%{name}.pc

%changelog
* Sat Jan 20 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 7.0-1
- Add option to send IPv6 address in EPSV response
- Add function to get the command string
- Terminate the connection if server fails to write the 220 banner
- Fix typo in GridFTP server response type

* Wed Nov 01 2017 Globus Toolkit <support@globus.org> - 6.1-1
- Don't error if acquire_cred fails when vhost env is set

* Wed Sep 06 2017 Globus Toolkit <support@globus.org> - 6.0-1
- Add support for control channel over TLS

* Mon Aug 07 2017 Globus Toolkit <support@globus.org> - 5.2-1
- allow 400 responses to stat failures

* Thu Jul 13 2017 Globus Toolkit <support@globus.org> - 5.1-1
- fix mem error on empty mlsc responses

* Fri Mar 03 2017 Globus Toolkit <support@globus.org> - 5.0-1
- extend response_type to allow for ftp error codes

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 4.2-4
- Rebuild after changes for el.5 with openssl101e

* Thu Aug 25 2016 Globus Toolkit <support@globus.org> - 4.2-2
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 4.2-1
- Update bug report URL

* Tue May 03 2016 Globus Toolkit <support@globus.org> - 4.1-1
- Spelling

* Mon Nov 23 2015 Globus Toolkit <support@globus.org> - 4.0-1
- Add correct behavior for data auth error code

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 3.7-2
- Add vendor

* Wed Jul 01 2015 Globus Toolkit <support@globus.org> - 3.7-1
- remove dead code

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 3.6-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 3.5-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 3.5-1
- Merge changes from Mattias Ellert

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 3.4-1
- Version bump for consistency

* Tue Mar 11 2014 Globus Toolkit <support@globus.org> - 3.3-1
- Fix leak

* Thu Feb 27 2014 Globus Toolkit <support@globus.org> - 3.2-1
- Packaging fixes, Warning Cleanup

* Wed Feb 12 2014 Globus Toolkit <support@globus.org> - 3.1-1
- Packaging fixes

* Tue Jan 21 2014 Globus Toolkit <support@globus.org> - 3.0-1
- Repackage for GT6 without GPT

* Tue Oct 15 2013 Globus Toolkit <support@globus.org> - 2.10-1
- GT-472: GridFTP server fails to detect client disconnection with piplining

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 2.9-2
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Wed Jun 05 2013 Globus Toolkit <support@globus.org> - 2.9-1
- GT-396: fix mlst on filenames that end in a newline
- GT-412: add -version-tag to set an identifier in the server version string
- fix minor memory leaks
- fix mlsx symlink target not urlencoding properly

* Wed Mar 06 2013  Globus Toolkit <support@globus.org> - 2.8-2
- Add missing build dependency

* Mon Feb 04 2013 Globus Toolkit <support@globus.org> - 2.8-1
- GT-302: Add initial sharing support to the GridFTP server
- GT-356: Add configuration and a command to make the sharing authorization file easier to manage

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 2.7-3
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 2.7-2
- GT 5.2.2 final

* Thu Jul 12 2012 Joseph Bester <bester@mcs.anl.gov> - 2.7-1
- GT-172: Removed custom MLSx tag feature
- GT-244: Cleaned up memory leaks
- GT-243: Fix needless frontend->backend connections

* Thu May 17 2012 Joseph Bester <bester@mcs.anl.gov> - 2.6-1
- GT-195: GridFTP acts as wrong user when user doesn't exist

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 2.5-2
- RHEL 4 patches

* Tue Mar 06 2012 Joseph Bester <bester@mcs.anl.gov> - 2.5-1
- GRIDFTP-165: correct chunking of MLSC response
- GRIDFTP-165: fix MLSC over split processes
- GRIDFTP-198: performance improvements for control channel messages
- GRIDFTP-201: Add heartbeat/status markers to CKSM and RETR
- GRIDFTP-222: fix threaded issues with streaming dir info for mlsd and mlsc

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 2.4-1
- RIC-226: Some dependencies are missing in GPT metadata

* Tue Dec 06 2011 Joseph Bester <bester@mcs.anl.gov> - 2.3-1
- fix mlst double space

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 2.2-4
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 2.2-3
- Last sync prior to 5.2.0

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 2.1-2
- Add explicit dependencies on >= 5.2 libraries

* Thu Oct 06 2011 Joseph Bester <bester@mcs.anl.gov> - 2.1-1
- Add backward-compatibility aging

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 2.0-3
- Fix missing whitespace in Requires

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 2.0-2
- Update for 5.1.2 release

* Sat Jul 17 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.43-1
- Update to Globus Toolkit 5.0.2

* Wed Apr 14 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.42-1
- Update to Globus Toolkit 5.0.1

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.40-1
- Update to Globus Toolkit 5.0.0

* Tue Jul 28 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.36-1
- Autogenerated
