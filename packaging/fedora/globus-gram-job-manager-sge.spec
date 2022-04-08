%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-gram-job-manager-sge
%global _name %(echo %{name} | tr - _)
Version:	3.3
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Grid Engine Job Manager Support

Group:		Applications/Internet
#		The sge.pm file is LGPLv2, the rest is ASL 2.0
License:	%{?suse_version:Apache-2.0 and LGPL-2.1}%{!?suse_version:ASL 2.0 and LGPLv2}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	gcc
BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-scheduler-event-generator-devel >= 4
%if ! %{?suse_version}%{!?suse_version:0}
BuildRequires:	perl-generators
BuildRequires:	perl-interpreter
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
Provides:	globus-gram-job-manager-setup-sge = 2.6
Obsoletes:	globus-gram-job-manager-setup-sge < 2.6
Obsoletes:	globus-gram-job-manager-setup-sge-doc < 2.6

%package setup-poll
Summary:	Grid Community Toolkit - Grid Engine Job Manager Support using polling
Group:		Applications/Internet
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6
BuildArch:	noarch
%endif
Provides:	%{name}-setup = %{version}-%{release}
Requires:	%{name} = %{version}-%{release}

Requires(preun):	globus-gram-job-manager-scripts >= 4

%package setup-seg
Summary:	Grid Community Toolkit - Grid Engine Job Manager Support using SEG
Group:		Applications/Internet
Provides:	%{name}-setup = %{version}-%{release}
Requires:	%{name}%{?_isa} = %{version}-%{release}
Requires:	globus-scheduler-event-generator-progs >= 4
%if %{?suse_version}%{!?suse_version:0}
Obsoletes:	libglobus_seg_sge < %{version}-%{release}
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
Grid Engine Job Manager Support

%description setup-poll
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-setup-poll package contains:
Grid Engine Job Manager Support using polling to monitor job state

%description setup-seg
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-setup-seg package contains:
Grid Engine Job Manager Support using the scheduler event generator to monitor
job state

%prep
%setup -q -n %{_name}-%{version}

%build
export QSUB=%{_bindir}/qsub-ge
export QSTAT=%{_bindir}/qstat-ge
export QDEL=%{_bindir}/qdel-ge
export QCONF=%{_bindir}/qconf
export MPIRUN=no
export SUN_MPRUN=no
%configure --disable-static \
	   --includedir=%{_includedir}/globus \
	   --libexecdir=%{_datadir}/globus \
	   --docdir=%{_pkgdocdir} \
	   --with-perlmoduledir=%{perl_vendorlib} \
	   --with-globus-state-dir=%{_localstatedir}/log/globus \
	   --with-sge-config=%{_sysconfdir}/sysconfig/gridengine \
	   --with-sge-root=undefined \
	   --with-sge-cell=undefined \
	   --without-queue-validation \
	   --without-pe-validation

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT

# Remove libtool archives (.la files)
rm $RPM_BUILD_ROOT%{_libdir}/*.la

# Remove jobmanager-sge from install dir - leave it for admin configuration
rm $RPM_BUILD_ROOT%{_sysconfdir}/grid-services/jobmanager-sge

%preun setup-poll
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-sge-poll > /dev/null 2>&1 || :
fi

%preun setup-seg
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-sge-seg > /dev/null 2>&1 || :
    /sbin/service globus-scheduler-event-generator stop sge > /dev/null 2>&1 || :
    globus-scheduler-event-generator-admin -d sge > /dev/null 2>&1 || :
fi

%post setup-seg -p /sbin/ldconfig

%postun setup-seg
/sbin/ldconfig
if [ $1 -ge 1 ]; then
    /sbin/service globus-scheduler-event-generator condrestart sge > /dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%{_datadir}/globus/globus_gram_job_manager/sge.rvf
%dir %{perl_vendorlib}/Globus
%dir %{perl_vendorlib}/Globus/GRAM
%dir %{perl_vendorlib}/Globus/GRAM/JobManager
%{perl_vendorlib}/Globus/GRAM/JobManager/sge.pm
%config(noreplace) %{_sysconfdir}/globus/globus-sge.conf
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/AUTHORS
%doc %{_pkgdocdir}/CREDITS
%doc %{_pkgdocdir}/GLOBUS_LICENSE
%doc %{_pkgdocdir}/LICENSE*

%files setup-poll
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-sge-poll

%files setup-seg
%defattr(-,root,root,-)
# This is a loadable module (plugin)
%{_libdir}/libglobus_seg_sge.so
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-sge-seg
%config(noreplace) %{_sysconfdir}/globus/scheduler-event-generator/available/sge

%changelog
* Fri Mar 11 2022 Mattias Ellert <mattias.ellert@physics.uu.se> - 3.3-1
- Fix some compiler warnings

* Fri Aug 20 2021 Mattias Ellert <mattias.ellert@physics.uu.se> - 3.2-1
- Typo fixes

* Thu Mar 12 2020 Mattias Ellert <mattias.ellert@physics.uu.se> - 3.1-2
- Add BuildRequires perl-interpreter

* Thu Jul 18 2019 Mattias Ellert <mattias.ellert@physics.uu.se> - 3.1-1
- Add AC_CONFIG_MACRO_DIR and ACLOCAL_AMFLAGS

* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 3.0-1
- First Grid Community Toolkit release

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 2.6-8
- Rebuild after changes for el.5 with openssl101e

* Mon Aug 29 2016 Globus Toolkit <support@globus.org> - 2.6-7
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 2.6-1
- Update bug report URL

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 2.5-2
- Add vendor

* Thu Jan 22 2015 Globus Toolkit <support@globus.org> - 2.5-1
- Handle UGE 8.2.0 timestamp format change

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 2.4-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 2.3-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 2.3-1
- Merge changes from Mattias Ellert

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 2.2-1
- Version bump for consistency

* Sat Feb 15 2014 Globus Toolkit <support@globus.org> - 2.1-1
- Packaging fixes

* Wed Jan 22 2014 Globus Toolkit <support@globus.org> - 2.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 1.7-2
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Tue May 21 2013 Globus Toolkit <support@globus.org> - 1.7-1
- solves an issue where globus gets confused at midnight about running jobs

* Fri Mar 08 2013 Globus Toolkit <support@globus.org> - 1.6-3
- Dependency updates

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 1.6-2
- Workaround missing F18 doxygen/latex dependency

* Wed Feb 13 2013 Globus Toolkit <support@globus.org> - 1.6-1
- GT-359: SGE SEG hangs when log_path points to directory

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 1.5-6
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 1.5-5
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 1.5-4
- GT 5.2.2 Release

* Thu May 24 2012 Joseph Bester <bester@mcs.anl.gov> - 1.5-3
- use qstat-ge and co. on rhel5 as well

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

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 1.0-7
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 1.0-6
- Last sync prior to 5.2.0

* Fri Oct 21 2011 Joseph Bester <bester@mcs.anl.gov> - 1.0-5
- Fix %%post* scripts to check for -eq 1
- Add explicit dependencies on >= 5.2 libraries

* Thu Sep 22 2011 Joseph Bester <bester@mcs.anl.gov> - 1.0-4

* Mon Sep 12 2011 Joseph Bester <bester@mcs.anl.gov> - 1.0-3
- Update path to qsub, etc for RHEL5 / EPEL

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 1.0-2
- Update for 5.1.2 release

