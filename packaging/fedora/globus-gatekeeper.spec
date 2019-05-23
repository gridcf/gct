%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-gatekeeper
%global _name %(echo %{name} | tr - _)
Version:	11.2
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Globus Gatekeeper

Group:		Applications/Internet
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	gcc
BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-gss-assist-devel >= 8
BuildRequires:	globus-gssapi-gsi-devel >= 9
%if %{?suse_version}%{!?suse_version:0}
BuildRequires:	libopenssl-devel
%else
BuildRequires:	openssl-devel
%endif
%if %{?suse_version}%{!?suse_version:0}
BuildRequires:	insserv
%endif

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
Requires(preun):	globus-common-progs >= 14
Requires(postun):	globus-common-progs >= 14

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Globus Gatekeeper

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

if [ "%{_initddir}" != "%{_sysconfdir}/init.d" ] ; then
    mkdir -p $RPM_BUILD_ROOT%{_initddir}
    mv $RPM_BUILD_ROOT%{_sysconfdir}/init.d/* $RPM_BUILD_ROOT%{_initddir}
    rmdir $RPM_BUILD_ROOT%{_sysconfdir}/init.d
fi

mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/grid-services
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/grid-services/available

%post
%if %{?suse_version}%{!?suse_version:0}
%fillup_and_insserv %{name}
%else
if [ $1 -eq 1 ]; then
    /sbin/chkconfig --add %{name}
fi
%endif

%preun
%if %{?suse_version}%{!?suse_version:0}
%stop_on_removal %{name}
%else
if [ $1 -eq 0 ]; then
    /sbin/service %{name} stop > /dev/null 2>&1 || :
    /sbin/chkconfig --del %{name}
fi
%endif

%postun
%if %{?suse_version}%{!?suse_version:0}
%restart_on_update %{name}
%insserv_cleanup
%else
if [ $1 -ge 1 ]; then
    /sbin/service %{name} condrestart > /dev/null 2>&1 || :
fi
%endif

%files
%defattr(-,root,root,-)
%{_sbindir}/globus-gatekeeper
%{_sbindir}/globus-k5
%{_initddir}/%{name}
%if %{?suse_version}%{!?suse_version:0}
%{_localstatedir}/adm/fillup-templates/sysconfig.%{name}
%else
%config(noreplace) %{_sysconfdir}/sysconfig/%{name}
%endif
%config(noreplace) %{_sysconfdir}/logrotate.d/%{name}
%dir %{_sysconfdir}/grid-services
%dir %{_sysconfdir}/grid-services/available
%doc %{_mandir}/man8/globus-gatekeeper.8*
%doc %{_mandir}/man8/globus-k5.8*
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%changelog
* Thu Apr 25 2019 Mattias Ellert <mattias.ellert@physics.uu.se> - 11.2-1
- Fix broken init.d script (lsb version)

* Wed Apr 17 2019 Mattias Ellert <mattias.ellert@physics.uu.se> - 11.1-1
- Remove obsolete acconfig.h file

* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 11.0-1
- First Grid Community Toolkit release
- Remove support for openssl101e (RHEL5 is EOL)
- Fix make clean rule

* Fri Sep 09 2016 Globus Toolkit <support@globus.org> - 10.12-1
- Updates for el.5 openssl101e

* Mon Aug 29 2016 Globus Toolkit <support@globus.org> - 10.11-4
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 10.11-1
- Update bug report URL

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 10.10-2
- Add vendor

* Mon Apr 06 2015 Globus Toolkit <support@globus.org> - 10.10-1
- Remove Dead Code
- Require minimal lsb-core for RHEL 6+ and fedora 20+

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 10.9-1
- doxygen fixes

* Thu Sep 18 2014 Globus Toolkit <support@globus.org> - 10.8-1
- GT-455: Incorporate OSG patches
- GT-465: OSG patch "gatekeeper-logrotate-copytruncate.patch" for globus-gatekeeper

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 10.7-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 10.6-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 10.6-1
- Merge changes from Mattias Ellert

* Fri May 23 2014 Globus Toolkit <support@globus.org> - 10.5-1
- Use globus_libc_[un]setenv

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 10.4-1
- Version bump for consistency

* Thu Feb 27 2014 Globus Toolkit <support@globus.org> - 10.3-1
- Packaging fixes, Warning Cleanup

* Tue Feb 25 2014 Globus Toolkit <support@globus.org> - 10.2-1
- Packaging fixes

* Fri Feb 14 2014 Globus Toolkit <support@globus.org> - 10.1-1
- Packaging fixes

* Thu Jan 23 2014 Globus Toolkit <support@globus.org> - 10.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 9.15-2
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Mon Mar 18 2013 Globus Toolkit <support@globus.org> - 9.15-1
- GT-354: Compatibility with automake 1.13

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 9.14-2
- 5.2.3

* Tue Jul 17 2012 Joseph Bester <bester@mcs.anl.gov> - 9.14-1
- GT-253: gatekeeper and job manager don't build on hurd

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 9.13-3
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 9.13-2
- GT 5.2.2 Release

* Thu May 24 2012 Joseph Bester <bester@mcs.anl.gov> - 9.13-1
- GT-205: gatekeeper should log a message when it exits due to the presence of /etc/nologin

* Mon May 14 2012 Joseph Bester <bester@mcs.anl.gov> - 9.12-1
- GT-159: globus-gatekeeper init script should report errors better

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 9.11-3
- RHEL 4 patches

* Mon May 07 2012 Joseph Bester <bester@mcs.anl.gov> - 9.11-1
- Updates for SUSE 11

* Fri Apr 13 2012 Joseph Bester <bester@mcs.anl.gov> - 9.11-1
- RIC-258: Can't rely on MKDIR_P

* Fri Apr 06 2012 Joseph Bester <bester@mcs.anl.gov> - 9.10-1
- GRAM-335: init scripts fail on solaris because of stop alias
- RIC-205: Missing directories $GLOBUS_LOCATION/var/lock and $GLOBUS_LOCATION/var/run

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 9.9-1
- GRAM-303: Gatekeeper's syslog output cannot be controlled
- GRAM-309: GRAM5 doesn't work with IPv4 only gatekeepers
- RIC-226: Some dependencies are missing in GPT metadata

* Fri Jan 06 2012 Joseph Bester <bester@mcs.anl.gov> - 9.7-1
- GRAM-303: Gatekeeper's syslog output cannot be controlled

* Mon Dec 12 2011 Joseph Bester <bester@mcs.anl.gov> - 9.6-1
- init script fixes

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 9.5-3
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 9.5-2
- Last sync prior to 5.2.0

* Mon Nov 28 2011 Joseph Bester <bester@mcs.anl.gov> - 9.5-1
- GRAM-285: Set default gatekeeper log in native packages

* Mon Nov 28 2011 Joseph Bester <bester@mcs.anl.gov> - 9.4-1
- GRAM-287: Hang of globus-gatekeeper process

* Wed Nov 23 2011 Joseph Bester <bester@mcs.anl.gov> - 9.3-1
- Updated version numbers

* Tue Nov 15 2011 Joseph Bester <bester@mcs.anl.gov> - 9.2-1
- GRAM-276: Increase backlog for gatekeeper

* Mon Nov 07 2011 Joseph Bester <bester@mcs.anl.gov> - 9.1-1
- Add default chkconfig line

* Mon Nov 07 2011 Joseph Bester <bester@mcs.anl.gov> - 9.0-1
- GRAM-268: GRAM requires gss_export_sec_context to work

* Fri Oct 28 2011 Joseph Bester <bester@mcs.anl.gov> - 8.2-1
- GRAM-267: globus-gatekeeper uses inappropriate Default-Start in init script

* Fri Oct 21 2011 Joseph Bester <bester@mcs.anl.gov> - 8.1-2
- Fix %%post* scripts to check for -eq 1
- Add explicit dependencies on >= 5.2 libraries

* Fri Sep 23 2011 Joseph Bester <bester@mcs.anl.gov> - 8.1-1
- GRAM-260: Detect and workaround bug in start_daemon for LSB < 4

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 8.0-2
- Update for 5.1.2 release

* Mon Apr 25 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.7-4
- Add README file

* Tue Apr 19 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.7-3
- Add start-up script and README.Fedora file

* Mon Feb 28 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.7-2
- Fix typos in the setup patch

* Thu Feb 24 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.7-1
- Update to Globus Toolkit 5.0.3

* Tue Feb 08 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 5.5-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_15_Mass_Rebuild

* Sat Jul 17 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.5-2
- Simplify directory ownership

* Wed Apr 14 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.5-1
- Update to Globus Toolkit 5.0.1

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.3-1
- Update to Globus Toolkit 5.0.0

* Wed Jul 29 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.0-1
- Autogenerated
