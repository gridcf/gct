Name:		globus-xio-popen-driver
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	3.6
Release:	3%{?dist}
Summary:	Globus Toolkit - Globus XIO Pipe Open Driver

Group:		System Environment/Libraries
License:	%{apache_license}
URL:		http://toolkit.globus.org/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-xio-devel >= 3
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
Summary:	Globus Toolkit - Globus XIO Pipe Open Driver
Group:		System Environment/Libraries
%endif

%package devel
Summary:	Globus Toolkit - Globus XIO Pipe Open Driver Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
Requires:	globus-xio-devel%{?_isa} >= 3

%if %{?suse_version}%{!?suse_version:0} >= 1315
%description %{?nmainpkg}
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{mainpkg} package contains:
Globus XIO Pipe Open Driver - allows a user to execute a program and treat it
as a transport driver by routing data through pipes
%endif

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
Globus XIO Pipe Open Driver - allows a user to execute a program and treat it
as a transport driver by routing data through pipes

%description devel
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-devel package contains:
Globus XIO Pipe Open Driver Development Files

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

find $RPM_BUILD_ROOT%{_libdir} -name 'lib*.la' -exec rm -vf '{}' \;

%check
make %{?_smp_mflags} check

%clean
rm -rf $RPM_BUILD_ROOT

%post %{?nmainpkg} -p /sbin/ldconfig

%postun %{?nmainpkg} -p /sbin/ldconfig

%files %{?nmainpkg}
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}
%{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%{_libdir}/libglobus*.so*

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*
%{_libdir}/pkgconfig/*.pc

%changelog
* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 3.6-3
- Rebuild after changes for el.5 with openssl101e

* Thu Aug 25 2016 Globus Toolkit <support@globus.org> - 3.6-2
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 3.6-1
- Update bug report URL

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 3.5-2
- Add vendor

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 3.5-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 3.4-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 3.4-1
- Merge changes from Mattias Ellert

* Tue May 06 2014 Globus Toolkit <support@globus.org> - 3.3-1
- Don't version dynamic module

* Thu Apr 24 2014 Globus Toolkit <support@globus.org> - 3.2-2
- Fix .so in filelist

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 3.2-1
- Version bump for consistency

* Thu Feb 13 2014 Globus Toolkit <support@globus.org> - 3.1-1
- Packaging Fixes

* Wed Jan 22 2014 Globus Toolkit <support@globus.org> - 3.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 2.3-7
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Tue Mar 05 2013 Globus Toolkit <support@globus.org> - 2.3-6
- Add missing dependencies

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 2.3-5
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 2.3-4
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 2.3-3
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 2.3-2
- RHEL 4 patches

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 2.3-1
- RIC-226: Some dependencies are missing in GPT metadata

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 2.2-3
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 2.2-2
- Last sync prior to 5.2.0

* Fri Nov 04 2011 Joseph Bester <bester@mcs.anl.gov> - 2.2-1
- Allow ECHILD in close without causing an error
- Pass const down stack to avoid bad cast

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 2.1-2
- Add explicit dependencies on >= 5.2 libraries

* Thu Oct 06 2011 Joseph Bester <bester@mcs.anl.gov> - 2.1-1
- Add backward-compatibility aging

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 2.0-2
- Update for 5.1.2 release

* Wed Apr 14 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.9-1
- Update to Globus Toolkit 5.0.1

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.7-1
- Update to Globus Toolkit 5.0.0

* Thu Jul 23 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.2-4
- Add instruction set architecture (isa) tags

* Thu Jun 04 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.2-3
- Update to official Fedora Globus packaging guidelines

* Fri Apr 24 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.2-2
- Correct package description

* Thu Apr 16 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.2-1
- Make comment about source retrieval more explicit
- Change defines to globals
- Remove explicit requires on library packages
- Put GLOBUS_LICENSE file in extracted source tarball

* Sun Mar 15 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.2-0.5
- Adapting to updated globus-core package

* Thu Feb 26 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.2-0.4
- Add s390x to the list of 64 bit platforms

* Tue Dec 30 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.2-0.3
- Adapt to updated GPT package

* Mon Oct 20 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.2-0.2
- Update to Globus Toolkit 4.2.1

* Tue Jul 15 2008 Mattias Ellert <mattias.ellert@fysast.uu.se> - 0.2-0.1
- Autogenerated
