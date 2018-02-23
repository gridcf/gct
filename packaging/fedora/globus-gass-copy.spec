Name:		globus-gass-copy
%global soname 2
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	9.28
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Globus Gass Copy

Group:		System Environment/Libraries
License:	%{apache_license}
URL:		https://github.com/gridcf/gct/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

%if %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  openssl
BuildRequires:  libopenssl-devel
%else
%if %{?rhel}%{!?rhel:0} == 5
BuildRequires:  openssl101e
BuildRequires:  openssl101e-devel
BuildConflicts: openssl-devel
%else
BuildRequires:  openssl
BuildRequires:  openssl-devel
%endif
%endif

BuildRequires:	globus-ftp-client-devel >= 7
BuildRequires:	globus-common-devel >= 15
BuildRequires:	globus-gssapi-gsi-devel >= 9
BuildRequires:	globus-io-devel >= 8
BuildRequires:	globus-gass-transfer-devel >= 7
BuildRequires:	globus-ftp-control-devel >= 4
BuildRequires:	globus-gridftp-server-progs
BuildRequires:	globus-gridftp-server-devel
BuildRequires:	globus-xio-gsi-driver-devel
BuildRequires:	globus-xio-pipe-driver-devel
BuildRequires:	doxygen
BuildRequires:	graphviz
%if %{?rhel}%{!?rhel:0} == 5
BuildRequires:	graphviz-gd
%endif
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  automake >= 1.11
BuildRequires:  autoconf >= 2.60
BuildRequires:  libtool >= 2.2
%endif
BuildRequires:  pkgconfig
%if %{?fedora}%{!?fedora:0} >= 18 || %{?rhel}%{!?rhel:0} >= 6
BuildRequires:  perl-Test-Simple
%endif
BuildRequires: perl(URI)
%if 0%{?suse_version} > 0
BuildRequires: libtool
%else
BuildRequires: libtool-ltdl-devel
%endif

%if %{?suse_version}%{!?suse_version:0} >= 1315
%global mainpkg lib%{_name}%{soname}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0} != 0
%package %{?nmainpkg}
Summary:	Grid Community Toolkit - Globus Gass Copy
Group:		System Environment/Libraries
%endif

%package progs
Summary:	Grid Community Toolkit - Globus Gass Copy Programs
Group:		Applications/Internet
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}

%package devel
Summary:	Grid Community Toolkit - Globus Gass Copy Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
Requires:	globus-ftp-client-devel%{?_isa} >= 7
Requires:	globus-common-devel%{?_isa} >= 15
Requires:	globus-gssapi-gsi-devel%{?_isa} >= 9
Requires:	globus-io-devel%{?_isa} >= 8
Requires:	globus-gass-transfer-devel%{?_isa} >= 7
Requires:	globus-ftp-control-devel%{?_isa} >= 4

%package doc
Summary:	Grid Community Toolkit - Globus Gass Copy Documentation Files
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
Globus Gass Copy
%endif

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
Globus Gass Copy

%description progs
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-progs package contains:
Globus Gass Copy Programs

%description devel
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-devel package contains:
Globus Gass Copy Development Files

%description doc
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-doc package contains:
Globus Gass Copy Documentation Files

%prep
%setup -q -n %{_name}-%{version}

%build
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
# Remove files that should be replaced during bootstrap
rm -rf autom4te.cache

autoreconf -if
%endif

%if %{?rhel}%{!?rhel:0} == 5
export OPENSSL="$(which openssl101e)"
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
make %{?_smp_mflags} check

%clean
rm -rf $RPM_BUILD_ROOT

%post %{?nmainpkg} -p /sbin/ldconfig

%postun %{?nmainpkg} -p /sbin/ldconfig

%files %{?nmainpkg}
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}
%doc %{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%{_libdir}/libglobus*.so.*

%files progs
%defattr(-,root,root,-)
%{_bindir}/*
%{_mandir}/man1/*

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
* Mon Sep 25 2017 Globus Toolkit <support@globus.org> - 9.28-1
- add popen driver to cygwin static builds

* Fri Apr 21 2017 Globus Toolkit <support@globus.org> - 9.27-1
- prevent some race conditions

* Fri Apr 21 2017 Globus Toolkit <support@globus.org> - 9.26-1
- remove checksum data from public handle

* Thu Apr 20 2017 Globus Toolkit <support@globus.org> - 9.25-1
- Fix uninitialized field related crash

* Fri Mar 10 2017 Globus Toolkit <support@globus.org> - 9.24-1
- don't attempt sshftp data protection without creds
- checksum verification based on contribution from IBM

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 9.23-1
- Update for el.5 openssl101e, replace docbook with asciidoc

* Fri Aug 26 2016 Globus Toolkit <support@globus.org> - 9.22-3
- Updates for SLES 12

* Fri Aug 19 2016 Globus Toolkit <support@globus.org> - 9.22-1
- Fix tests run as root

* Thu Aug 18 2016 Globus Toolkit <support@globus.org> - 9.21-1
- Makefile fix

* Tue Aug 16 2016 Globus Toolkit <support@globus.org> - 9.20-1
- Updates for OpenSSL 1.1.0

* Mon Apr 18 2016 Globus Toolkit <support@globus.org> - 9.19-1
- Use prelinks for tests so that they run on El Capitan

* Fri Nov 20 2015 Globus Toolkit <support@globus.org> - 9.18-1
- Disable ipv6 default for tests

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 9.17-2
- Add vendor

* Tue Jul 28 2015 Globus Toolkit <support@globus.org> - 9.17-1
- use SIGINT to terminating test server for gcov

* Wed Jul 01 2015 Globus Toolkit <support@globus.org> - 9.16-1
- Improve error handling
- Fix non-terminated string

* Wed Apr 08 2015 Globus Toolkit <support@globus.org> - 9.15-1
- Clarify documentation of stack options
- Add openssl build dependency

* Wed Apr 08 2015 Globus Toolkit <support@globus.org> - 9.14-1
- Fix user-specified data channel stack handling

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 9.13-1
- doxygen fixes

* Mon Sep 22 2014 Globus Toolkit <support@globus.org> - 9.12-1
- Include more manpages for API
- Fix some Doxygen issues
- Quiet some autoconf/automake warnings

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 9.11-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 9.10-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 9.10-1
- Merge changes from Mattias Ellert

* Thu Apr 24 2014 Globus Toolkit <support@globus.org> - 9.9-1
- Packaging fixes

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 9.8-1
- Version bump for consistency

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 9.7-1
- Version bump for consistency

* Tue Feb 25 2014 Globus Toolkit <support@globus.org> - 9.6-1
- Packaging fixes

* Thu Feb 20 2014 Globus Toolkit <support@globus.org> - 9.5-1
- Test fixes

* Thu Feb 20 2014 Globus Toolkit <support@globus.org> - 9.4-1
- Test fixes

* Thu Feb 20 2014 Globus Toolkit <support@globus.org> - 9.3-1
- Test fixes

* Wed Feb 19 2014 Globus Toolkit <support@globus.org> - 9.2-1
- Packaging fixes

* Wed Jan 22 2014 Globus Toolkit <support@globus.org> - 9.1-1
- Repackage for GT6 without GPT

* Wed Jan 22 2014 Globus Toolkit <support@globus.org> - 9.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 8.6-4
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 8.6-3
- Workaround missing F18 doxygen/latex dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 8.6-2
- 5.2.3

* Tue Jul 17 2012 Joseph Bester <bester@mcs.anl.gov> - 8.6-1
- GT-241: wrong SIGINT handling in globus-url-copy

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 8.5-3
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 8.5-2
- GT 5.2.2 Release

* Wed Jun 27 2012 Joseph Bester <bester@mcs.anl.gov> - 8.5-1
- GRIDFTP-200: mixing ftp:// with -cred fails
- GRIDFTP-203: -create-dest fails when input is stdin
- GRIDFTP-208: Add manpage for globus-url-copy
- GRIDFTP-211: potentially unsafe format strings in globus-url-copy
- GRIDFTP-216: continue on error doesn't continue when a dir listing fails
- GRIDFTP-220: don't attempt mkdir when dir is known to exist.
- GT-153: make gridftp-v2 GET/PUT the default for server that support it
- RIC-224: Eliminate some doxygen warnings
- RIC-226: Some dependencies are missing in GPT metadata

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 8.4-3
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 8.4-2
- SLES 11 patches

* Tue Mar 06 2012 Joseph Bester <bester@mcs.anl.gov> - 8.4-1
- GRIDFTP-200: mixing ftp:// with -cred fails
- GRIDFTP-203: -create-dest fails when input is stdin
- GRIDFTP-216: continue on error doesn't continue when a dir listing fails
- GRIDFTP-220: don't attempt mkdir when dir is known to exist.

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 8.3-1
- GRIDFTP-208: Add manpage for globus-url-copy
- GRIDFTP-211: potentially unsafe format strings in globus-url-copy
- RIC-224: Eliminate some doxygen warnings
- RIC-226: Some dependencies are missing in GPT metadata

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 8.2-4
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 8.2-3
- Last sync prior to 5.2.0

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 8.1-2
- Add explicit dependencies on >= 5.2 libraries

* Thu Oct 06 2011 Joseph Bester <bester@mcs.anl.gov> - 8.1-1
- Add backward-compatibility aging

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 8.0-2
- Update for 5.1.2 release

* Sat Jul 17 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.7-1
- Update to Globus Toolkit 5.0.2

* Wed Apr 14 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.4-1
- Update to Globus Toolkit 5.0.1

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.3-1
- Update to Globus Toolkit 5.0.0

* Fri Aug 21 2009 Tomas Mraz <tmraz@redhat.com> - 4.14-4
- rebuilt with new openssl

* Thu Jul 23 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 4.14-3
- Add instruction set architecture (isa) tags
- Make doc subpackage noarch

* Thu Jun 04 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 4.14-2
- Update to official Fedora Globus packaging guidelines

* Thu Apr 16 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 4.14-1
- Make comment about source retrieval more explicit
- Change defines to globals
- Remove explicit requires on library packages
- Put GLOBUS_LICENSE file in extracted source tarball

* Sun Mar 15 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 4.14-0.5
- Adapting to updated globus-core package

* Thu Feb 26 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 4.14-0.4
- Add s390x to the list of 64 bit platforms

* Thu Jan 01 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 4.14-0.3
- Adapt to updated GPT package

* Tue Oct 21 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 4.14-0.2
- Update to Globus Toolkit 4.2.1

* Tue Jul 15 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 4.10-0.1
- Autogenerated
