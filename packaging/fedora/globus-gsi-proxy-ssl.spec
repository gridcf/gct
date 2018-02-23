Name:		globus-gsi-proxy-ssl
%global soname 1
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif

%global _name %(tr - _ <<< %{name})
Version:	5.10
Release:	1%{?dist}
Summary:	Globus Toolkit - Globus GSI Proxy SSL Library

Group:		System Environment/Libraries
License:	%{apache_license}
URL:		http://toolkit.globus.org/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	doxygen

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

BuildRequires:	graphviz
%if "%{?rhel}" == "5"
BuildRequires:	graphviz-gd
%endif
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:	automake >= 1.11
BuildRequires:	autoconf >= 2.60
BuildRequires:	libtool >= 2.2
%endif
BuildRequires:  pkgconfig
%if %{?fedora}%{!?fedora:0} >= 18 || %{?rhel}%{!?rhel:0} >= 6
BuildRequires:  perl-Test-Simple
%endif
%if %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:	globus-common-devel%{?_isa} >= 14
%endif

%if %{?suse_version}%{!?suse_version:0} >= 1315
%global mainpkg libglobus_proxy_ssl%{soname}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif


%if %{?nmainpkg:1}%{!?nmainpkg:0} != 0
%package %{?nmainpkg}
Summary:	Globus Toolkit - Globus GSI Proxy SSL Library
Group:		System Environment/Libraries
%endif

%package devel
Summary:	Globus Toolkit - Globus GSI Proxy SSL Library Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
%if %{?suse_version}%{!?suse_version:0} >= 1315
Requires:	globus-common-devel%{?_isa} >= 14
%endif
%if %{?suse_version}%{!?suse_version:0} >= 1315
Requires:  openssl
Requires:  libopenssl-devel
%else
%if %{?rhel}%{!?rhel:0} == 5
Requires:  openssl101e
Requires:  openssl101e-devel
%else
Requires:  openssl
Requires:  openssl-devel
%endif
%endif

%package doc
Summary:	Globus Toolkit - Globus GSI Proxy SSL Library Documentation Files
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
Globus GSI Proxy SSL Library
%endif

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
Globus GSI Proxy SSL Library

%description devel
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-devel package contains:
Globus GSI Proxy SSL Library Development Files

%description doc
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-doc package contains:
Globus GSI Proxy SSL Library Documentation Files

%prep
%setup -q -n %{_name}-%{version}

%build
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
# Remove files that should be replaced during bootstrap
rm -rf autom4te.cache

autoreconf -if
%endif

%if %{?rhel}%{!?rhel:0}
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
%{_libdir}/libglobus_*so.*

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*.h
%{_libdir}/libglobus_*so
%{_libdir}/pkgconfig/%{name}.pc


%files doc
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}/html
%{_docdir}/%{name}-%{version}/html/*
%{_mandir}/*/*

%changelog
* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 5.10-1
- Update for el.5 openssl101e

* Wed Sep 07 2016 Globus Toolkit <support@globus.org> - 5.9-6
- Update el.5 build to use OpenSSL 1.0.1e from EPEL

* Thu Aug 25 2016 Globus Toolkit <support@globus.org> - 5.9-5
- Updates for SLES 12 packaging

* Tue Aug 16 2016 Globus Toolkit <support@globus.org> - 5.9-1
- Updates for OpenSSL 1.1.0 compatibility

* Fri May 20 2016 Globus Toolkit <support@globus.org> - 5.8-1
- Fix strchr of non-terminated string when displaying policy

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 5.7-2
- Add vendor

* Thu Sep 25 2014 Globus Toolkit <support@globus.org> - 5.7-1
- Include more manpages for API
- Doxygen markup fixes
- Fix typos and clarify some documentation
- Quiet some autoconf/automake warnings

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 5.6-3
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 5.6-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 5.6-1
- Merge changes from Mattias Ellert

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 5.5-1
- Version bump for consistency

* Thu Feb 27 2014 Globus Toolkit <support@globus.org> - 5.4-1
- Packaging fixes, Warning Cleanup

* Tue Feb 25 2014 Globus Toolkit <support@globus.org> - 5.3-1
- Packaging fixes

* Mon Feb 10 2014 Globus Toolkit <support@globus.org> - 5.1-1
- Packaging fixes

* Tue Jan 21 2014 Globus Toolkit <support@globus.org> - 5.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 4.1-12
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 4.1-11
- Workaround missing F18 doxygen/latex dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 4.1-10
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 4.1-9
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 4.1-8
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 4.1-7
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 4.1-6
- SLES 11 patches

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 4.1-5
- Updated version numbers

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 4.1-4
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 4.1-3
- Last sync prior to 5.2.0

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 4.1-2
- Add explicit dependencies on >= 5.2 libraries

* Thu Oct 06 2011 Joseph Bester <bester@mcs.anl.gov> - 4.1-1
- Add backward-compatibility aging

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 4.0-2
- Update for 5.1.2 release

* Sat Jul 17 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.3-1
- Update to Globus Toolkit 5.0.2
- Drop patch globus-gsi-proxy-ssl-oid.patch (fixed upstream)

* Mon May 31 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.2-2
- Fix OID registration pollution

* Tue Apr 13 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.2-1
- Update to Globus Toolkit 5.0.1
- Drop patches globus-gsi-proxy-ssl-asn1-method.patch and
  globus-gsi-proxy-ssl-typo.patch (fixed upstream)

* Fri Jan 22 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.0-1
- Update to Globus Toolkit 5.0.0

* Fri Aug 21 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.5-6
- Fix for new openssl - ASN1_METHOD no longer supported

* Fri Aug 21 2009 Tomas Mraz <tmraz@redhat.com> - 1.5-5
- rebuilt with new openssl

* Thu Jul 23 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.5-4
- Add instruction set architecture (isa) tags
- Make doc subpackage noarch

* Wed Jun 03 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.5-3
- Update to official Fedora Globus packaging guidelines

* Tue Apr 28 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.5-2
- Rebuild with updated libtool

* Wed Apr 15 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.5-1
- Make comment about source retrieval more explicit
- Change defines to globals
- Put GLOBUS_LICENSE file in extracted source tarball

* Sun Mar 15 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.5-0.5
- Adapting to updated globus-core package

* Thu Feb 26 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.5-0.4
- Add s390x to the list of 64 bit platforms

* Thu Jan 01 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.5-0.3
- Adapt to updated GPT package

* Mon Oct 13 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.5-0.2
- Update to Globus Toolkit 4.2.1

* Mon Jul 14 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.5-0.1
- Autogenerated
