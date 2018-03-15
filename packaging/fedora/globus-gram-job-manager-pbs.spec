Name:		globus-gram-job-manager-pbs
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	2.6
Release:	4%{?dist}
Summary:	Grid Community Toolkit - PBS Job Manager

Group:		Applications/Internet
License:	%{apache_license}
URL:		https://github.com/gridcf/gct/
Source:	%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
Obsoletes:      globus-gram-job-manager-setup-pbs < 4.5

Requires:       globus-gram-job-manager-scripts >= 4
Requires:	globus-gass-cache-program >= 5
Requires:	globus-common-progs >= 14
%if 0%{?suse_version} > 0
    %if %{suse_version} < 1140
Requires:     perl = %{perl_version}
    %else
%{perl_requires}
    %endif
%else
Requires:	perl(:MODULE_COMPAT_%(eval "`perl -V:version`"; echo $version))
%endif
BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-xio-devel >= 3
BuildRequires:	globus-scheduler-event-generator-devel >= 4
BuildRequires:	globus-gram-protocol-devel >= 11
BuildRequires:	doxygen
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  automake >= 1.11
BuildRequires:  autoconf >= 2.60
BuildRequires:  libtool >= 2.2
%endif
BuildRequires:  pkgconfig

%if %{?suse_version}%{!?suse_version:0} >= 1315
%package -n libglobus_seg_pbs
Summary:        Grid Community Toolkit - PBS Job Manager SEG Module
Group:		Applications/Internet
%endif

%package setup-poll
Summary:        Grid Community Toolkit - PBS Job Manager Setup Files
Group:		Applications/Internet
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6
BuildArch:      noarch
%endif
Provides:       %{name}-setup
Provides:       globus-gram-job-manager-setup
Requires:	%{name} = %{version}-%{release}
requires(post): globus-gram-job-manager-scripts >= 3.4
requires(preun): globus-gram-job-manager-scripts >= 3.4
Conflicts:      %{name}-setup-seg

%package setup-seg
Summary:	Grid Community Toolkit - PBS Job Manager Setup Files
Group:		Applications/Internet
Provides:       %{name}-setup
Provides:       globus-gram-job-manager-setup
Requires:	%{name} = %{version}-%{release}
%if %{?suse_version}%{!?suse_version:0} >= 1315
Requires:	libglobus_seg_pbs = %{version}-%{release}
%endif
PreReq:         globus-scheduler-event-generator-progs >= 4
PreReq: 	globus-gram-job-manager-scripts >= 4
Requires(post): globus-gram-job-manager-scripts >= 4
Requires(post): globus-scheduler-event-generator-progs >= 4
Requires(preun): globus-gram-job-manager-scripts >= 4
Requires(preun): globus-scheduler-event-generator-progs >= 4
Conflicts:      %{name}-setup-poll

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
PBS Job Manager 

%description setup-poll
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
PBS Job Manager Setup using polling to monitor job state

%if %{?suse_version}%{!?suse_version:0} >= 1315
%description -n libglobus_seg_pbs
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The libglobus_seg_pbs package contains:
PBS Job Manager SEG Module
%endif

%description setup-seg
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
PBS Job Manager Setup using SEG to monitor job state

%prep
%setup -q -n %{_name}-%{version}

%build
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
# Remove files that should be replaced during bootstrap
rm -rf autom4te.cache

autoreconf -if
%endif

export MPIEXEC=no
export MPIRUN=no
export QDEL=/usr/bin/qdel-torque
export QSTAT=/usr/bin/qstat-torque
export QSUB=/usr/bin/qsub-torque
%global pbs_log_path /var/log/torque/server_logs

%configure \
           --disable-static \
           --docdir=%{_docdir}/%{name}-%{version} \
           --includedir=%{_includedir}/globus \
           --libexecdir=%{_datadir}/globus \
           --with-globus-state-dir=%{_localstatedir}/lib/globus \
           --with-log-path=%{pbs_log_path} \
           --with-perlmoduledir=%{perl_vendorlib}

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT
# Remove jobmanager-pbs from install dir so that it can be
# added/removed by post scripts
rm $RPM_BUILD_ROOT/etc/grid-services/jobmanager-pbs

# Remove libtool archives (.la files)
find $RPM_BUILD_ROOT%{_libdir} -name 'lib*.la' -exec rm -v '{}' \;
%post setup-poll
if [ $1 -eq 1 ]; then
    globus-gatekeeper-admin -e jobmanager-pbs-poll -n jobmanager-pbs > /dev/null 2>&1 || :
    if [ ! -f /etc/grid-services/jobmanager ]; then
        globus-gatekeeper-admin -e jobmanager-pbs-poll -n jobmanager
    fi
fi

%preun setup-poll
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-pbs-poll > /dev/null 2>&1 || :
fi

%postun setup-poll
if [ $1 -eq 1 ]; then
    globus-gatekeeper-admin -e jobmanager-pbs-poll -n jobmanager-pbs > /dev/null 2>&1 || :
elif [ $1 -eq 0 -a ! -f /etc/grid-services/jobmanager ]; then
    globus-gatekeeper-admin -E > /dev/null 2>&1 || :
fi

%if %{?suse_version}%{!?suse_version:0} >= 1315
%post -n libglobus_seg_pbs
ldconfig

%postun -n libglobus_seg_pbs
ldconfig
%endif

%post setup-seg
%if %{?suse_version}%{!?suse_version:0} < 1315
ldconfig
%endif
if [ $1 -eq 1 ]; then
    globus-gatekeeper-admin -e jobmanager-pbs-seg -n jobmanager-pbs > /dev/null 2>&1 || :
    globus-scheduler-event-generator-admin -e pbs > /dev/null 2>&1 || :
    service globus-scheduler-event-generator condrestart pbs
fi

%preun setup-seg
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-pbs-seg > /dev/null 2>&1 || :
    globus-scheduler-event-generator-admin -d pbs > /dev/null 2>&1 || :
    service globus-scheduler-event-generator stop pbs > /dev/null 2>&1 || :
fi

%postun setup-seg
%if %{?suse_version}%{!?suse_version:0} < 1315
ldconfig
%endif
if [ $1 -eq 1 ]; then
    globus-gatekeeper-admin -e jobmanager-pbs-seg > /dev/null 2>&1 || :
    globus-scheduler-event-generator-admin -e pbs > /dev/null 2>&1 || :
    service globus-scheduler-event-generator condrestart pbs > /dev/null 2>&1 || :
elif [ $1 -eq 0 -a ! -f /etc/grid-services/jobmanager ]; then
    globus-gatekeeper-admin -E > /dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}
%dir %{_sysconfdir}/globus
%config(noreplace) %{_sysconfdir}/globus/globus-pbs.conf
%{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%dir %{_datadir}/globus
%dir %{_datadir}/globus/globus_gram_job_manager
%{_datadir}/globus/globus_gram_job_manager/pbs.rvf
%dir %{perl_vendorlib}/Globus
%dir %{perl_vendorlib}/Globus/GRAM
%dir %{perl_vendorlib}/Globus/GRAM/JobManager
%dir %{_sysconfdir}/grid-services
%dir %{_sysconfdir}/grid-services/available
%{perl_vendorlib}/Globus/GRAM/JobManager/pbs.pm

%files setup-poll
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-pbs-poll

%if %{?suse_version}%{!?suse_version:0} >= 1315
%files -n libglobus_seg_pbs
%defattr(-,root,root,-)
%{_libdir}/libglobus*
%endif

%files setup-seg
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-pbs-seg
%dir %{_sysconfdir}/globus/scheduler-event-generator
%dir %{_sysconfdir}/globus/scheduler-event-generator/available
%{_sysconfdir}/globus/scheduler-event-generator/available/pbs
%if %{?suse_version}%{!?suse_version:0} < 1315
%{_libdir}/libglobus_*
%endif

%changelog
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

