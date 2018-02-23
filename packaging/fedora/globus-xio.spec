Name:		globus-xio
%global soname 0
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif


%global _name %(tr - _ <<< %{name})
Version:	5.16
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Globus XIO Framework

Group:		System Environment/Libraries
License:	%{apache_license}
URL:		http://toolkit.globus.org/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	globus-common-devel >= 14
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
%if %{?suse_version}%{!?suse_version:0} == 0
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
Summary:	Grid Community Toolkit - Globus XIO Framework
Group:		System Environment/Libraries
%endif

%package devel
Summary:	Grid Community Toolkit - Globus XIO Framework Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
Requires:	globus-common-devel%{?_isa} >= 14

%package doc
Summary:	Grid Community Toolkit - Globus XIO Framework Documentation Files
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
Globus XIO Framework
%endif

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{mainpkg} package contains:
Globus XIO Framework

%description devel
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-devel package contains:
Globus XIO Framework Development Files

%description doc
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-doc package contains:
Globus XIO Framework Documentation Files

%prep

%setup -q -n %{_name}-%{version}

unset GLOBUS_LOCATION
unset GPT_LOCATION

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

# Fix doxygen glitches
for f in $RPM_BUILD_ROOT%{_mandir}/man3/globus_xio_driver.3 \
	 $RPM_BUILD_ROOT%{_mandir}/man3/GLOBUS_XIO_API_ASSIST.3 ; do
  sed 's/P\.RS/P\n.RS/' -i $f
done


%check
GLOBUS_HOSTNAME=localhost make %{?_smp_mflags} check

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
* Wed May 24 2017 Globus Toolkit <support@globus.org> - 5.16-1
- Fix crash in error handling in http driver

* Mon Mar 20 2017 Globus Toolkit <support@globus.org> - 5.15-1
- Don't rely on globus_error_put(NULL) to be GLOBUS_SUCCESS

* Fri Nov 04 2016 Globus Toolkit <support@globus.org> - 5.14-1
- Don't crash when GLOBUS_TCP_PORT_RANGE has the same min and max

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 5.13-4
- Rebuild after changes for el.5 with openssl101e

* Wed Aug 24 2016 Globus Toolkit <support@globus.org> - 5.13-3
- SLES 12 packaging conditionals

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 5.13-1
- Update bug report URL

* Tue Apr 05 2016 Globus Toolkit <support@globus.org> - 5.12-1
- fix test driver load problem on mac

* Mon Nov 23 2015 Globus Toolkit <support@globus.org> - 5.11-1
- fix failures connecting to v4mapped addresses on systems that disable dual stack sockets by default

* Tue Oct 27 2015 Globus Toolkit <support@globus.org> - 5.10-1
- Clarify documentation for timeouts
- Remove NET+OS fragments

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 5.9-2
- Add vendor

* Wed Jul 01 2015 Globus Toolkit <support@globus.org> - 5.9-1
- Allow const string option names
- Fix miscount of string length in GLOBUS_XIO_GET_STRING_OPTIONS
- Fix some error handling bugs
- Remove some unused variables

* Tue Apr 07 2015 Globus Toolkit <support@globus.org> - 5.8-1
- Check push result in globus_xio_driver_list_to_stack_attr()
- Add doc for globus_xio_driver_list_to_stack_attr

* Thu Feb 12 2015 Globus Toolkit <support@globus.org> - 5.7-1
- more GT-581 tweaks

* Thu Feb 12 2015 Globus Toolkit <support@globus.org> - 5.6-1
- GT-581: Prefer IPV6 address family when creating a listener on all interfaces

* Mon Feb 09 2015 Globus Toolkit <support@globus.org> - 5.5-1
- GT-581: Prefer IPV6 address family when creating a listener on all interfaces

* Fri Jan 09 2015 Globus Toolkit <support@globus.org> - 5.4-1
- Better fix for testing on localhost

* Tue Jan 06 2015 Globus Toolkit <support@globus.org> - 5.3-1
- Fix TAP output of test

* Mon Dec 22 2014 Globus Toolkit <support@globus.org> - 5.2-1
- Fix hang in tests on cygwin

* Mon Dec 22 2014 Globus Toolkit <support@globus.org> - 5.1-1
- Fix hang in tests on cygwin

* Thu Dec 18 2014 Globus Toolkit <support@globus.org> - 5.0-1
- Add net_manager support

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 4.17-1
- doxygen fixes

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 4.16-1
- Use localhost for tests

* Thu Sep 25 2014 Globus Toolkit <support@globus.org> - 4.15-1
- Use consistent PREDEFINED in all Doxyfiles
- Doxygen markup fixes
- Fix typos and clarify some documentation
- Quiet some autoconf/automake warnings

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 4.14-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 4.13-2
- Fix Source path

* Mon Aug 11 2014 Globus Toolkit <support@globus.org> - 4.13-1
- Fix regression caused by GT-546 fix

* Thu Aug 07 2014 Globus Toolkit <support@globus.org> - 4.12-1
- GT-546: HTTP transfers larger than 4GB fail 

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 4.11-1
- Merge changes from Mattias Ellert

* Tue May 27 2014 Globus Toolkit <support@globus.org> - 4.10-1
- Use package-named config.h

* Thu Apr 24 2014 Globus Toolkit <support@globus.org> - 4.9-1
- Packaging fixes

* Sat Apr 19 2014 Globus Toolkit <support@globus.org> - 4.8-1
- Test fixes

* Sat Apr 19 2014 Globus Toolkit <support@globus.org> - 4.7-1
- Test fixes

* Sat Apr 19 2014 Globus Toolkit <support@globus.org> - 4.6-1
- Make sure IOV_MAX equivalent is figured out

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 4.5-1
- Version bump for consistency

* Thu Feb 27 2014 Globus Toolkit <support@globus.org> - 4.3-1
- Packaging fixes, Warning Cleanup

* Tue Feb 25 2014 Globus Toolkit <support@globus.org> - 4.2-1
- Packaging fixes

* Fri Feb 07 2014 Globus Toolkit <support@globus.org> - 4.1-1
- Fix test case

* Mon Jan 27 2014 Globus Toolkit <support@globus.org> - 4.0-1
- Add tests to xio package
- Fix issues with .pc.in files
- Merge branch 'toplevel_makefile' of https://github.com/globus/globus-toolkit into globus_6_branch
- Native packaging for globus-xio from GT6 branch
- New version of rectify-versions
- Opt for POSIX 1003.1-2001 (pax) format tarballs
- Remove GPT and make-packages.pl from build process
- autoconf/automake updates

* Thu Oct 10 2013 Globus Toolkit <support@globus.org> - 3.6-1
- GT-445: Doxygen fixes

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 3.5-2
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Sat Jun 01 2013 Globus Toolkit <support@globus.org> - 3.5-1
- Fix wrapblock drivers losing attrs

* Mon Mar 18 2013 Globus Toolkit <support@globus.org> - 3.4-1
- GT-354: Compatibility with automake 1.13

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 3.3-7
- Workaround missing F18 doxygen/latex dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 3.3-6
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 3.3-5
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 3.3-4
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 3.3-3
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 3.3-2
- SLES 11 patches

* Mon Mar 05 2012 Joseph Bester <bester@mcs.anl.gov> - 3.3-1
- RIC-240: fix memory leak when GLOBUS_XIO_ATTR_SET_CREDENTIAL is used
- RIC-241: check return value of close()

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 3.2-5
- Updated version numbers

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 3.2-4
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 3.2-3
- Last sync prior to 5.2.0

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 3.1-2
- Add explicit dependencies on >= 5.2 libraries

* Thu Oct 06 2011 Joseph Bester <bester@mcs.anl.gov> - 3.1-1
- Add backward-compatibility aging

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 3.0-2
- Update for 5.1.2 release

* Fri Jan 22 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.8-2
- Update to Globus Toolkit 5.0.0

* Wed Jul 29 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.8-1
- Update to upstream update release 2.8

* Thu Jul 23 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.7-5
- Add instruction set architecture (isa) tags
- Make doc subpackage noarch

* Wed Jun 03 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.7-4
- Update to official Fedora Globus packaging guidelines

* Mon Apr 27 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.7-3
- Rebuild with updated libtool

* Mon Apr 20 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.7-2
- Put GLOBUS_LICENSE file in extracted source tarball

* Thu Apr 16 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.7-1
- Make comment about source retrieval more explicit
- Change defines to globals
- Remove explicit requires on library packages

* Sun Mar 15 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.7-0.5
- Adapting to updated globus-core package

* Thu Feb 26 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.7-0.4
- Add s390x to the list of 64 bit platforms

* Thu Jan 01 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.7-0.3
- Adapt to updated GPT package

* Mon Oct 20 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.7-0.2
- Update to Globus Toolkit 4.2.1

* Mon Jul 14 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.7-0.1
- Autogenerated
