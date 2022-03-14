%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-scheduler-event-generator
%global soname 0
%global _name %(echo %{name} | tr - _)
Version:	6.5
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Scheduler Event Generator

Group:		System Environment/Libraries
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	gcc
BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-xio-devel >= 3
BuildRequires:	globus-gram-protocol-devel >= 11
BuildRequires:	globus-xio-gsi-driver-devel >= 2
%if %{?suse_version}%{!?suse_version:0}
BuildRequires:	libtool
%else
BuildRequires:	libtool-ltdl-devel
%endif
BuildRequires:	doxygen
%if %{?suse_version}%{!?suse_version:0} > 0
BuildRequires:	insserv
%endif
#		Additional requirements for make check
BuildRequires:	perl-interpreter
BuildRequires:	perl(File::Basename)
BuildRequires:	perl(File::Compare)
BuildRequires:	perl(File::Temp)
BuildRequires:	perl(Test::More)

%if %{?suse_version}%{!?suse_version:0}
%global mainpkg lib%{_name}%{soname}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0}
%package %{?nmainpkg}
Summary:	Grid Community Toolkit - Scheduler Event Generator
Group:		System Environment/Libraries
Provides:	%{name} = %{version}-%{release}
Obsoletes:	%{name} < %{version}-%{release}
%endif

Requires:	globus-xio-gsi-driver%{?_isa} >= 2

%package progs
Summary:	Grid Community Toolkit - Scheduler Event Generator Programs
Group:		Applications/Internet
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}

%if %{?suse_version}%{!?suse_version:0}
Requires(post):		%insserv_prereq %fillup_prereq
Requires(preun):	%insserv_prereq %fillup_prereq
Requires(postun):	%insserv_prereq %fillup_prereq
%else
Requires(post):		chkconfig
Requires(preun):	chkconfig
Requires(preun):	initscripts
Requires(postun):	initscripts
Requires(preun):	lsb-core-noarch
Requires(postun):	lsb-core-noarch
%endif
Requires(post):		globus-common-progs >= 14
Requires(postun):	globus-common-progs >= 14

%package devel
Summary:	Grid Community Toolkit - Scheduler Event Generator Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}

%package doc
Summary:	Grid Community Toolkit - Scheduler Event Generator Documentation Files
Group:		Documentation
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6
BuildArch:	noarch
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0}
%description %{?nmainpkg}
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{mainpkg} package contains:
Scheduler Event Generator
%endif

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Scheduler Event Generator

%description progs
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-progs package contains:
Scheduler Event Generator Programs

%description devel
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-devel package contains:
Scheduler Event Generator Development Files

%description doc
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-doc package contains:
Scheduler Event Generator Documentation Files

%prep
%setup -q -n %{_name}-%{version}

%build
%configure --disable-static \
	   --includedir=%{_includedir}/globus \
	   --libexecdir=%{_datadir}/globus \
	   --docdir=%{_pkgdocdir} \
	   --with-lsb \
%if %{?suse_version}%{!?suse_version:0}
	   --with-default-runlevels=235 \
	   --with-initscript-config-path=%{_localstatedir}/adm/fillup-templates/sysconfig.%{name} \
%else
	   --with-initscript-config-path=%{_sysconfdir}/sysconfig/%{name} \
%endif
	   --with-lockfile-path=%{_localstatedir}/lock/subsys/%{name}

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT

# Remove libtool archives (.la files)
rm $RPM_BUILD_ROOT%{_libdir}/*.la

if [ "%{_initddir}" != "%{_sysconfdir}/init.d" ] ; then
    mkdir -p $RPM_BUILD_ROOT%{_initddir}
    mv $RPM_BUILD_ROOT%{_sysconfdir}/init.d/* $RPM_BUILD_ROOT%{_initddir}
    rmdir $RPM_BUILD_ROOT%{_sysconfdir}/init.d
fi

%check
make %{?_smp_mflags} check VERBOSE=1

%post %{?nmainpkg} -p /sbin/ldconfig

%postun %{?nmainpkg} -p /sbin/ldconfig

%post progs
%if %{?suse_version}%{!?suse_version:0}
%fillup_and_insserv %{name}
%else
if [ $1 -eq 1 ]; then
    /sbin/chkconfig --add %{name}
fi
%endif

%preun progs
%if %{?suse_version}%{!?suse_version:0}
%stop_on_removal %{name}
%else
if [ $1 -eq 0 ]; then
    /sbin/chkconfig --del %{name}
    /sbin/service %{name} stop > /dev/null 2>&1 || :
fi
%endif

%postun progs
%if %{?suse_version}%{!?suse_version:0}
%restart_on_update %{name}
%insserv_cleanup
%else
if [ $1 -ge 1 ]; then
    /sbin/service %{name} condrestart > /dev/null 2>&1 || :
fi
%endif

%files %{?nmainpkg}
%defattr(-,root,root,-)
%{_libdir}/libglobus_scheduler_event_generator.so.*
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%files progs
%defattr(-,root,root,-)
%{_sbindir}/globus-scheduler-event-generator
%{_sbindir}/globus-scheduler-event-generator-admin
%{_mandir}/man8/globus-scheduler-event-generator.8*
%{_mandir}/man8/globus-scheduler-event-generator-admin.8*
%if %{?suse_version}%{!?suse_version:0}
%{_localstatedir}/adm/fillup-templates/sysconfig.%{name}
%else
%config(noreplace) %{_sysconfdir}/sysconfig/%{name}
%endif
%{_initddir}/%{name}
%dir %{_sysconfdir}/globus
%dir %{_sysconfdir}/globus/scheduler-event-generator
%dir %{_sysconfdir}/globus/scheduler-event-generator/available

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*
%{_libdir}/libglobus_scheduler_event_generator.so
%{_libdir}/pkgconfig/%{name}.pc

%files doc
%defattr(-,root,root,-)
%doc %{_mandir}/man3/*
%dir %{_pkgdocdir}
%dir %{_pkgdocdir}/html
%doc %{_pkgdocdir}/html/*
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%changelog
* Thu Mar 10 2022 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.5-1
- Fix some compiler warnings

* Thu Dec 17 2020 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.4-1
- Keep admin script in sync with init script

* Tue Mar 10 2020 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.3-2
- Add BuildRequires perl-interpreter
- Add additional perl dependencies for tests

* Tue Mar 10 2020 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.3-1
- Remove unused TESTS.pl script

* Tue Mar 10 2020 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.2-1
- Make makefiles exit sooner on errors

* Wed Nov 21 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.1-1
- Doxygen fixes

* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.0-1
- First Grid Community Toolkit release

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 5.12-5
- Rebuild after changes for el.5 with openssl101e

* Fri Aug 26 2016 Globus Toolkit <support@globus.org> - 5.12-4
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 5.12-1
- Update bug report URL

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 5.11-2
- Add vendor

* Mon Apr 06 2015 Globus Toolkit <support@globus.org> - 5.11-1
- Remove dead code
- Depend on lsb-core when possible

* Fri Jan 09 2015 Globus Toolkit <support@globus.org> - 5.10-2
- Better fix for testing on localhost

* Fri Jan 09 2015 Globus Toolkit <support@globus.org> - 5.10-1
- Missing -avoid-version (and remove duplicated compiler options)

* Mon Nov 17 2014 Globus Toolkit <support@globus.org> - 5.9-1
- Fix globus-scheduler-event-generator script paths

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 5.8-2
- Manpage format mistake

* Tue Sep 30 2014 Globus Toolkit <support@globus.org> - 5.7-2
- Add .txt documentation to filelist

* Tue Sep 23 2014 Globus Toolkit <support@globus.org> - 5.7-1
- Use mixed case man page install for all packages
- Doxygen markup fixes
- Fix broken globus-scheduler-event-generator-admin script
- Add documentation for globus-scheduler-event-generator and globus-scheduler-event-generator-admin
- Quiet some autoconf/automake warnings

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 5.6-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 5.5-2
- Fix Source path

* Wed Aug 06 2014 Globus Toolkit <support@globus.org> - 5.5-1
- Incorrect argument order to globus_cond_wait

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 5.4-1
- Merge changes from Mattias Ellert

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 5.3-1
- Version bump for consistency

* Thu Feb 27 2014 Globus Toolkit <support@globus.org> - 5.2-1
- Packaging fixes, Warning Cleanup

* Thu Feb 13 2014 Globus Toolkit <support@globus.org> - 5.1-1
- Test fixes

* Tue Jan 21 2014 Globus Toolkit <support@globus.org> - 5.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 4.7-4
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 4.7-3
- Workaround missing F18 doxygen/latex dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 4.7-2
- 5.2.3

* Tue Oct 09 2012 Globus Toolkit <support@globus.org> - 4.7-1
- GT-295: Missing dependency in globus_scheduler_event_generator debian native packages

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 4.6-5
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 4.6-4
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 4.6-3
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 4.6-2
- SLES 11 patches

* Fri Apr 13 2012 Joseph Bester <bester@mcs.anl.gov> - 4.6-1
- RIC-258: Can't rely on MKDIR_P

* Fri Apr 06 2012 Joseph Bester <bester@mcs.anl.gov> - 4.5-1
- GRAM-335: init scripts fail on solaris because of stop alias
- RIC-205: Missing directories $GLOBUS_LOCATION/var/lock and $GLOBUS_LOCATION/var/run

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 4.4-2
- Updated version numbers

* Mon Dec 12 2011 Joseph Bester <bester@mcs.anl.gov> - 4.4-1
- init script fixes

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 4.3-3
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 4.3-2
- Last sync prior to 5.2.0

* Tue Nov 22 2011 Joseph Bester <bester@mcs.anl.gov> - 4.3-1
- GRAM-284: init defaults for debian

* Mon Oct 24 2011 Joseph Bester <bester@mcs.anl.gov> - 4.2-2
- Add explicit dependencies on >= 5.2 libraries
- Add backward-compatibility aging
- Fix %%post* scripts to check for -eq 1

* Fri Sep 23 2011 Joseph Bester <bester@mcs.anl.gov> - 4.1-1
- GRAM-260: Detect and workaround bug in start_daemon for LSB < 4

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 4.0-2
- Update for 5.1.2 release

* Mon Apr 25 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.1-4
- Add README file

* Tue Feb 08 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 2.1-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_15_Mass_Rebuild

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.1-2
- Update to Globus Toolkit 5.0.0

* Wed Jul 29 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 2.1-1
- Autogenerated
