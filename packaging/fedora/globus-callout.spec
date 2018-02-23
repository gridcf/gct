Name:		globus-callout
%global soname 0
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	3.15
Release:	3%{?dist}
Summary:	Grid Community Toolkit - Globus Callout Library

Group:		System Environment/Libraries
License:	%{apache_license}
URL:		https://github.com/gridcf/gct/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Requires:	globus-common%{?_isa} >= 15

%if 0%{?suse_version} > 0
BuildRequires: libtool
%else
BuildRequires: libtool-ltdl-devel
%endif
BuildRequires:	globus-common-devel >= 15
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
Summary:	Grid Community Toolkit - Globus Callout Library
Group:		System Environment/Libraries
%endif

%package devel
Summary:	Grid Community Toolkit - Globus Callout Library Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
Requires:	globus-common-devel%{?_isa} >= 15

%package doc
Summary:	Grid Community Toolkit - Globus Callout Library Documentation Files
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
Globus Callout Library
%endif

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
Globus Callout Library - provides a platform independent way of dealing with
runtime loadable functions.

%description devel
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-devel package contains:
Globus Callout Library Development Files

%description doc
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-doc package contains:
Globus Callout Library Documentation Files

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
make ${_smp_mflags} check

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
* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 3.15-3
- Rebuild after changes for el.5 with openssl101e

* Thu Aug 25 2016 Globus Toolkit <support@globus.org> - 3.15-2
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 3.15-1
- Update bug report URL

* Thu Apr 07 2016 Globus Toolkit <support@globus.org> - 3.14-1
- Fix tests to run from installer on mac

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 3.13-2
- Add vendor

* Mon Sep 22 2014 Globus Toolkit <support@globus.org> - 3.13-1
- Doxygen markup fixes
- Use consistent flavor tags
- Use mixed case man page install for all packages
- Use consistent PREDEFINED in all Doxyfiles
- Fix dependency version
- Fix typos and clarify some documentation

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 3.12-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 3.11-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 3.11-1
- Merge changes from Mattias Ellert

* Fri May 23 2014 Globus Toolkit <support@globus.org> - 3.10-1
- Use globus_libc_unsetenv

* Tue Apr 22 2014 Globus Toolkit <support@globus.org> - 3.9-1
- Test fixes

* Mon Apr 21 2014 Globus Toolkit <support@globus.org> - 3.8-1
- Test fixes

* Mon Apr 21 2014 Globus Toolkit <support@globus.org> - 3.7-1
- Test fixes

* Mon Apr 21 2014 Globus Toolkit <support@globus.org> - 3.6-1
- Test fixes

* Mon Apr 21 2014 Globus Toolkit <support@globus.org> - 3.5-1
- Test fixes

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 3.4-1
- Version bump for consistency

* Mon Feb 17 2014 Globus Toolkit <support@globus.org> - 3.3-1
- Packaging fixes

* Fri Feb 07 2014 Globus Toolkit <support@globus.org> - 3.2-1
- Fix some configure problems

* Fri Feb 07 2014 Globus Toolkit <support@globus.org> - 3.1-1
- Dependencies

* Tue Jan 21 2014 Globus Toolkit <support@globus.org> - 3.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 2.4-2
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Thu Jun 06 2013 Globus Toolkit <support@globus.org> - 2.4-1
- GT-412: allow chaining of callouts and passing of env vars in the callout config

* Thu May 09 2013 Globus Toolkit <support@globus.org> - 2.3-1
- GT-390: globus_callout loader fails load plugins on systems with broken libtool-ltdl

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 2.2-7
- Workaround missing F18 doxygen/latex dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 2.2-6
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 2.2-5
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 2.2-4
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 2.2-3
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 2.2-2
- SLES 11 patches

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 2.2-1
- RIC-224: Eliminate some doxygen warnings
- RIC-230: Remove obsolete globus_libtool_windows code

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 2.1-4
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 2.1-3
- Last sync prior to 5.2.0

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 2.1-2
- Add explicit dependencies on >= 5.2 libraries

* Wed Oct 05 2011 Joseph Bester <bester@mcs.anl.gov> - 2.1-1
- Add backward-compatibility aging

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 2.0-2
- Update for 5.1.2 release

* Fri Jan 22 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.7-6
- Update to Globus Toolkit 5.0.0

* Thu Jul 23 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.7-5
- Add instruction set architecture (isa) tags
- Make doc subpackage noarch

* Wed Jun 03 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.7-4
- Update to official Fedora Globus packaging guidelines
- Allow loading callouts without flavor extensions

* Mon Apr 27 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.7-3
- Rebuild with updated libtool

* Mon Apr 20 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.7-2
- Put GLOBUS_LICENSE file in extracted source tarball

* Thu Apr 16 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.7-1
- Make comment about source retrieval more explicit
- Change defines to globals
- Remove explicit requires on library packages

* Sun Mar 15 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.7-0.5
- Adapting to updated globus-core package

* Thu Feb 26 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.7-0.4
- Add s390x to the list of 64 bit platforms

* Thu Jan 01 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.7-0.3
- Adapt to updated GPT package

* Wed Oct 15 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.7-0.2
- Update to Globus Toolkit 4.2.1

* Mon Jul 14 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.7-0.1
- Autogenerated
