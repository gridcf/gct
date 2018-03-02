Name:		globus-openssl-module
%global soname 0
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	4.8
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Globus OpenSSL Module Wrapper

Group:		System Environment/Libraries
License:	%{apache_license}
URL:		https://github.com/gridcf/gct/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	globus-gsi-proxy-ssl-devel >= 4
BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-gsi-openssl-error-devel >= 2

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

BuildRequires:	doxygen
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

%if %{?suse_version}%{!?suse_version:0} >= 1315
%global mainpkg libglobus_openssl%{soname}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0} != 0
%package %{?nmainpkg}
Summary:	Grid Community Toolkit - Globus OpenSSL Module Wrapper
Group:		System Environment/Libraries
%endif

%package devel
Summary:	Grid Community Toolkit - Globus OpenSSL Module Wrapper Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
Requires:	globus-gsi-proxy-ssl-devel%{?_isa} >= 4
Requires:	globus-common-devel%{?_isa} >= 14
Requires:	globus-gsi-openssl-error-devel%{?_isa} >= 2

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
Summary:	Grid Community Toolkit - Globus OpenSSL Module Wrapper Documentation Files
Group:		Documentation
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6
BuildArch:	noarch
%endif
Requires:	%{mainpkg} = %{version}-%{release}

%if %{?suse_version}%{!?suse_version:0} >= 1315
%description %{?nmainpkg}
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{mainpkg} package contains:
Globus OpenSSL Module Wrapper
%endif

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Globus OpenSSL Module Wrapper

%description devel
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-devel package contains:
Globus OpenSSL Module Wrapper Development Files

%description doc
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-doc package contains:
Globus OpenSSL Module Wrapper Documentation Files

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

%clean
rm -rf $RPM_BUILD_ROOT

%post %{?nmainpkg} -p /sbin/ldconfig

%postun %{?nmainpkg} -p /sbin/ldconfig

%files %{?nmainpkg}
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}
%doc %{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%{_libdir}/libglobus*.so.*

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*
%{_libdir}/pkgconfig/%{name}.pc
%{_libdir}/libglobus*.so

%files doc
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}/html
%{_docdir}/%{name}-%{version}/html/*
%{_mandir}/man3/*

%changelog
* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 4.8-1
- Update for el.5 openssl101e

* Thu Aug 25 2016 Globus Toolkit <support@globus.org> - 4.7-2
- Updates for SLES 12

* Tue Aug 16 2016 Globus Toolkit <support@globus.org> - 4.7-1
- Updates for OpenSSL 1.1.0

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 4.6-2
- Add vendor

* Thu Sep 25 2014 Globus Toolkit <support@globus.org> - 4.6-1
- Include more manpages for API
- Use consistent PREDEFINED in all Doxyfiles
- Doxygen markup fixes
- Quiet some autoconf/automake warnings

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 4.5-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 4.4-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 4.4-1
- Merge changes from Mattias Ellert

* Tue May 27 2014 Globus Toolkit <support@globus.org> - 4.3-1
- Don't require initializer

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 4.2-1
- Version bump for consistency

* Mon Jan 27 2014 Globus Toolkit <support@globus.org> - 4.1-1
- Add missing dependency

* Tue Jan 21 2014 Globus Toolkit <support@globus.org> - 4.0-1
- Repackage for GT6 without GPT

* Mon Jul 08 2013 Globus Toolkit <support@globus.org> - 3.3-4
- openssl-libs for newer packages

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 3.3-2
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Fri Jun 14 2013 Globus Toolkit <support@globus.org> - 3.3-1
- Be more tolerant if a symlink fails

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 3.2-7
- Workaround missing F18 doxygen/latex dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 3.2-6
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 3.2-5
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 3.2-4
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 3.2-3
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 3.2-2
- SLES 11 patches

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 3.2-1
- RIC-233: Move admin tool globus-update-certificate-dir to sbindir

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 3.1-4
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 3.1-3
- Last sync prior to 5.2.0

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 3.1-2
- Add explicit dependencies on >= 5.2 libraries

* Thu Oct 06 2011 Joseph Bester <bester@mcs.anl.gov> - 3.1-1
- Add backward-compatibility aging

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 3.0-2
- Update for 5.1.2 release

* Sat Jul 17 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.3-1
- Update to Globus Toolkit 5.0.2
- Drop patch globus-openssl-module-oid.patch (fixed upstream)

* Mon May 31 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.2-2
- Fix OID registration pollution

* Tue Apr 13 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.2-1
- Update to Globus Toolkit 5.0.1
- Drop patch globus-openssl-module-sslinit.patch (fixed upstream)

* Fri Jan 22 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 1.1-1
- Update to Globus Toolkit 5.0.0

* Fri Aug 21 2009 Tomas Mraz <tmraz@redhat.com> - 0.6-4
- rebuilt with new openssl

* Thu Jul 23 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-3
- Add instruction set architecture (isa) tags
- Make doc subpackage noarch

* Wed Jun 03 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-2
- Update to official Fedora Globus packaging guidelines

* Wed Apr 15 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-1
- Make comment about source retrieval more explicit
- Change defines to globals
- Remove explicit requires on library packages
- Put GLOBUS_LICENSE file in extracted source tarball

* Sun Mar 15 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-0.5
- Adapting to updated globus-core package

* Thu Feb 26 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-0.4
- Add s390x to the list of 64 bit platforms

* Thu Jan 01 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-0.3
- Adapt to updated GPT package

* Mon Oct 13 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-0.2
- Update to Globus Toolkit 4.2.1

* Mon Jul 14 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.6-0.1
- Autogenerated
