Name:		globus-gatekeeper
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	10.12
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Globus Gatekeeper

Group:		Applications/Internet
License:	%{apache_license}
URL:		https://github.com/gridcf/gct/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Requires:       psmisc

%if 0%{?suse_version} > 0
Requires:       insserv
Requires(post): %insserv_prereq  %fillup_prereq
%else
%if 0%{?rhel} >= 6 || 0%{?fedora} >= 20
Requires:       lsb-core-noarch
%else
Requires:       lsb
%endif
%endif

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

Requires(post): globus-common-progs >= 13.4
Requires(preun):globus-common-progs >= 13.4
%if 0%{?suse_version} > 0
BuildRequires:       insserv
%else
%if 0%{?rhel} >= 6 || 0%{?fedora} >= 20
BuildRequires:       lsb-core-noarch
%else
BuildRequires:       lsb
%endif
%endif
BuildRequires:	globus-gss-assist-devel >= 8
BuildRequires:	globus-gssapi-gsi-devel >= 9
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  automake >= 1.11
BuildRequires:  autoconf >= 2.60
BuildRequires:  libtool >= 2.2
%endif
BuildRequires:  pkgconfig

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
Globus Gatekeeper
Globus Gatekeeper Setup

%prep
%setup -q -n %{_name}-%{version}

%build
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
# Remove files that should be replaced during bootstrap
rm -rf autom4te.cache

autoreconf -if
%endif

%if %{?suse_version}%{!?suse_version:0} >= 1315
%global default_runlevels --with-default-runlevels=235
%global initscript_config_path %{_localstatedir}/adm/fillup-templates/sysconfig.%{name}
%else
%global initscript_config_path %{_sysconfdir}/sysconfig/%{name} 
%endif

%if %{?rhel}%{!?rhel:0} == 5
export OPENSSL="$(which openssl101e)"
%endif

%configure \
           --disable-static \
           --docdir=%{_docdir}/%{name}-%{version} \
           --includedir=%{_includedir}/globus \
           --libexecdir=%{_datadir}/globus \
           --with-lsb \
           %{?default_runlevels} \
	   --with-initscript-config-path=%{initscript_config_path} \
           --with-lockfile-path='${localstatedir}/lock/subsys/globus-gatekeeper'

make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
mkdir $RPM_BUILD_ROOT/etc/grid-services
mkdir $RPM_BUILD_ROOT/etc/grid-services/available

%check
make %{?_smp_mflags} check

%clean
rm -rf $RPM_BUILD_ROOT

%post
%if %{?suse_version}%{!?suse_version:0} >= 1315
%fillup_and_insserv globus-gatekeeper
%else
if [ $1 -eq 1 ]; then
    /sbin/chkconfig --add %{name}
fi
%endif

%preun
%if %{?suse_version}%{!?suse_version:0} >= 1315
%stop_on_removal service
%else
if [ $1 -eq 0 ]; then
    /sbin/chkconfig --del %{name}
    /sbin/service %{name} stop > /dev/null 2>&1 || :
fi
%endif

%postun
%if %{?suse_version}%{!?suse_version:0} >= 1315
%restart_on_update service
%insserv_cleanup
%else
if [ $1 -eq 1 ]; then
    /sbin/service %{name} condrestart > /dev/null 2>&1 || :
fi
%endif

%files
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}
%{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%dir /etc/grid-services
%dir /etc/grid-services/available
%config(noreplace) %{initscript_config_path}
%config(noreplace) /etc/logrotate.d/%{name}
%{_sysconfdir}/init.d/%{name}
%{_sbindir}/*
%{_mandir}/man8/*


%changelog
* Fri Sep 09 2016 Globus Toolkit <support@globus.org> - 10.12-1
- Updates for el.5 openssl101e

* Mon Aug 29 2016 Globus Toolkit <support@globus.org> - 10.11-4
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 10.11-1
- Update bug report URL

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 10.10-2
- Add vendor

* Mon Apr 06 2015 Globus Toolkit <support@globus.org> - 10.10-1
- - Remove Dead Code
- - Require minimal lsb-core for RHEL 6+ and fedora 20+

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
