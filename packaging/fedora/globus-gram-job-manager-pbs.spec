%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-gram-job-manager-pbs
%global _name %(echo %{name} | tr - _)
Version:	3.1
Release:	1%{?dist}
Summary:	Grid Community Toolkit - PBS Job Manager Support

Group:		Applications/Internet
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	gcc
BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-scheduler-event-generator-devel >= 4
%if ! %{?suse_version}%{!?suse_version:0}
BuildRequires:	perl-generators
%endif

Requires:	globus-gram-job-manager >= 13
Requires:	globus-gram-job-manager-scripts >= 4
Requires:	globus-gass-cache-program >= 5
Requires:	globus-gatekeeper >= 9
%if %{?suse_version}%{!?suse_version:0}
%{perl_requires}
%else
Requires:	perl(:MODULE_COMPAT_%(eval "`perl -V:version`"; echo $version))
%endif
Requires:	%{name}-setup = %{version}-%{release}
Provides:	globus-gram-job-manager-setup-pbs = 4.5
Obsoletes:	globus-gram-job-manager-setup-pbs < 4.5
Obsoletes:	globus-gram-job-manager-setup-pbs-doc < 4.5

%package setup-poll
Summary:	Grid Community Toolkit - PBS Job Manager Support using polling
Group:		Applications/Internet
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6
BuildArch:	noarch
%endif
Provides:	%{name}-setup = %{version}-%{release}
Requires:	%{name} = %{version}-%{release}

Requires(preun):	globus-gram-job-manager-scripts >= 4

%package setup-seg
Summary:	Grid Community Toolkit - PBS Job Manager Support using SEG
Group:		Applications/Internet
Provides:	%{name}-setup = %{version}-%{release}
Requires:	%{name}%{?_isa} = %{version}-%{release}
Requires:	globus-scheduler-event-generator-progs >= 4
%if %{?suse_version}%{!?suse_version:0}
Obsoletes:	libglobus_seg_pbs < %{version}-%{release}
%endif

Requires(preun):	globus-gram-job-manager-scripts >= 4
Requires(preun):	globus-scheduler-event-generator-progs >= 4
Requires(postun):	globus-scheduler-event-generator-progs >= 4
%if %{?suse_version}%{!?suse_version:0}
Requires(preun):	aaa-base
Requires(postun):	aaa-base
%else
Requires(preun):	initscripts
Requires(postun):	initscripts
%endif

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
PBS Job Manager Support

%description setup-poll
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-setup-poll package contains:
PBS Job Manager Support using polling to monitor job state

%description setup-seg
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-setup-seg package contains:
PBS Job Manager Support using the scheduler event generator to monitor job
state

%prep
%setup -q -n %{_name}-%{version}

%build
export MPIEXEC=no
export MPIRUN=no
export QDEL=%{_bindir}/qdel-torque
export QSTAT=%{_bindir}/qstat-torque
export QSUB=%{_bindir}/qsub-torque
%configure --disable-static \
	   --includedir=%{_includedir}/globus \
	   --libexecdir=%{_datadir}/globus \
	   --docdir=%{_pkgdocdir} \
	   --with-perlmoduledir=%{perl_vendorlib} \
	   --with-globus-state-dir=%{_localstatedir}/log/globus \
	   --with-log-path=%{_localstatedir}/log/torque/server_logs

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT

# Remove libtool archives (.la files)
rm $RPM_BUILD_ROOT%{_libdir}/*.la

# Remove jobmanager-pbs from install dir - leave it for admin configuration
rm $RPM_BUILD_ROOT%{_sysconfdir}/grid-services/jobmanager-pbs

%preun setup-poll
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-pbs-poll > /dev/null 2>&1 || :
fi

%preun setup-seg
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-pbs-seg > /dev/null 2>&1 || :
    /sbin/service globus-scheduler-event-generator stop pbs > /dev/null 2>&1 || :
    globus-scheduler-event-generator-admin -d pbs > /dev/null 2>&1 || :
fi

%post setup-seg -p /sbin/ldconfig

%postun setup-seg
/sbin/ldconfig
if [ $1 -ge 1 ]; then
    /sbin/service globus-scheduler-event-generator condrestart pbs > /dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%{_datadir}/globus/globus_gram_job_manager/pbs.rvf
%dir %{perl_vendorlib}/Globus
%dir %{perl_vendorlib}/Globus/GRAM
%dir %{perl_vendorlib}/Globus/GRAM/JobManager
%{perl_vendorlib}/Globus/GRAM/JobManager/pbs.pm
%config(noreplace) %{_sysconfdir}/globus/globus-pbs.conf
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%files setup-poll
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-pbs-poll

%files setup-seg
%defattr(-,root,root,-)
# This is a loadable module (plugin)
%{_libdir}/libglobus_seg_pbs.so
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-pbs-seg
%config(noreplace) %{_sysconfdir}/globus/scheduler-event-generator/available/pbs

%changelog
* Thu Jul 18 2019 Mattias Ellert <mattias.ellert@physics.uu.se> - 3.1-1
- Add AC_CONFIG_MACRO_DIR and ACLOCAL_AMFLAGS

* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 3.0-1
- First Grid Community Toolkit release

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 2.6-4
- Rebuild after changes for el.5 with openssl101e

* Mon Aug 29 2016 Globus Toolkit <support@globus.org> - 2.6-3
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 2.6-1
- Update bug report URL

* Wed Jan 20 2016 Globus Toolkit <support@globus.org> - 2.5-1
- Fix issue parsing torque v5.1.2 logs in SEG

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 2.4-2
- Add vendor

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 2.4-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 2.3-3
- Fix Source path

* Wed Jun 25 2014 Globus Toolkit <support@globus.org> - 2.3-2
- Remove empty doc package

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 2.3-1
- Merge changes from Mattias Ellert

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 2.2-1
- Version bump for consistency

* Tue Feb 11 2014 Globus Toolkit <support@globus.org> - 2.1-1
- Add getline implementations for older or non-POSIX systems

* Thu Jan 23 2014 Globus Toolkit <support@globus.org> - 2.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 1.6-5
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Thu Mar 14 2013 Globus Toolkit <support@globus.org> - 1.6-4
- Missing %%{?_isa} deps

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 1.6-3
- Workaround missing F18 doxygen/latex dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 1.6-2
- 5.2.3

* Wed Sep 12 2012 Joseph Bester <bester@mcs.anl.gov> - 1.6-1
- GT-276: PBS SEG module isn't robust against log files becoming unavailable

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 1.5-5
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 1.5-4
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 1.5-3
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 1.5-2
- SLES 11 patches

* Thu Apr 12 2012 Joseph Bester <bester@mcs.anl.gov> - 1.5-1
- GRAM-343: lrm packages grid-service files aren't in CLEANFILES

* Wed Mar 14 2012 Joseph Bester <bester@mcs.anl.gov> - 1.4-1
- GRAM-318: Periodic lockup of SEG

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 1.3-1
- GRAM-297: job manager service definitions contain unresolved variables
- GRAM-310: sge configure script error
- RIC-229: Clean up GPT metadata

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 1.1-4
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 1.1-3
- Last sync prior to 5.2.0

* Fri Oct 21 2011 Joseph Bester <bester@mcs.anl.gov> - 1.1-2
- Fix %%post* scripts to check for -eq 1
- Add explicit dependencies on >= 5.2 libraries

* Thu Sep 22 2011  <bester@mcs.anl.gov> - 1.1-1
- GRAM-253

* Thu Sep 22 2011 Joseph Bester <bester@mcs.anl.gov> - 1.0-4
- Change %%post check for -eq 1

* Mon Sep 12 2011 Joseph Bester <bester@mcs.anl.gov> - 1.0-3
- Change pbs_log_path for fedora 13 and rhel 5

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 1.0-2
- Update for 5.1.2 release

