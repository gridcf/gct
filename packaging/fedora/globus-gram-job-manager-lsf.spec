%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-gram-job-manager-lsf
%global _name %(echo %{name} | tr - _)
Version:	3.0
Release:	1%{?dist}
Summary:	Grid Community Toolkit - LSF Job Manager Support

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
Provides:	globus-gram-job-manager-setup-lsf = 2.6
Obsoletes:	globus-gram-job-manager-setup-lsf < 2.6
Obsoletes:	globus-gram-job-manager-setup-lsf-doc < 2.6

%package setup-poll
Summary:	Grid Community Toolkit - LSF Job Manager Support using polling
Group:		Applications/Internet
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6
BuildArch:	noarch
%endif
Provides:	%{name}-setup = %{version}-%{release}
Requires:	%{name} = %{version}-%{release}

Requires(preun):	globus-gram-job-manager-scripts >= 4

%package setup-seg
Summary:	Grid Community Toolkit - LSF Job Manager Support using SEG
Group:		Applications/Internet
Provides:	%{name}-setup = %{version}-%{release}
Requires:	%{name}%{?_isa} = %{version}-%{release}
Requires:	globus-scheduler-event-generator-progs >= 4
%if %{?suse_version}%{!?suse_version:0}
Obsoletes:	libglobus_seg_lsf < %{version}-%{release}
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
LSF Job Manager Support

%description setup-poll
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-setup-poll package contains:
LSF Job Manager Support using polling to monitor job state

%description setup-seg
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-setup-seg package contains:
LSF Job Manager Support using the scheduler event generator to monitor job
state

%prep
%setup -q -n %{_name}-%{version}

%build
export BSUB=%{_bindir}/bsub
export BQUEUES=%{_bindir}/bqueues
export BJOBS=%{_bindir}/bjobs
export BKILL=%{_bindir}/bkill
export BHIST=%{_bindir}/bhist
export BACCT=%{_bindir}/bacct
export MPIEXEC=no
export MPIRUN=no
%configure --disable-static \
	   --includedir=%{_includedir}/globus \
	   --libexecdir=%{_datadir}/globus \
	   --docdir=%{_pkgdocdir} \
	   --with-perlmoduledir=%{perl_vendorlib} \
	   --with-globus-state-dir=%{_localstatedir}/log/globus

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT

# Remove libtool archives (.la files)
rm $RPM_BUILD_ROOT%{_libdir}/*.la

# Remove jobmanager-lsf from install dir - leave it for admin configuration
rm $RPM_BUILD_ROOT%{_sysconfdir}/grid-services/jobmanager-lsf

%preun setup-poll
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-lsf-poll > /dev/null 2>&1 || :
fi

%preun setup-seg
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-lsf-seg > /dev/null 2>&1 || :
    /sbin/service globus-scheduler-event-generator stop lsf > /dev/null 2>&1 || :
    globus-scheduler-event-generator-admin -d lsf > /dev/null 2>&1 || :
fi

%post setup-seg -p /sbin/ldconfig

%postun setup-seg
/sbin/ldconfig
if [ $1 -ge 1 ]; then
    /sbin/service globus-scheduler-event-generator condrestart lsf > /dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%{_datadir}/globus/globus_gram_job_manager/lsf.rvf
%dir %{perl_vendorlib}/Globus
%dir %{perl_vendorlib}/Globus/GRAM
%dir %{perl_vendorlib}/Globus/GRAM/JobManager
%{perl_vendorlib}/Globus/GRAM/JobManager/lsf.pm
%config(noreplace) %{_sysconfdir}/globus/globus-lsf.conf
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%files setup-poll
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-lsf-poll

%files setup-seg
%defattr(-,root,root,-)
# This is a loadable module (plugin)
%{_libdir}/libglobus_seg_lsf.so
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-lsf-seg
%config(noreplace) %{_sysconfdir}/globus/scheduler-event-generator/available/lsf

%changelog
* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 3.0-1
- First Grid Community Toolkit release

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 2.7-4
- Rebuild after changes for el.5 with openssl101e

* Thu Aug 25 2016 Globus Toolkit <support@globus.org> - 2.7-3
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 2.7-1
- Update bug report URL

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 2.6-2
- Add vendor

* Mon Sep 22 2014 Globus Toolkit <support@globus.org> - 2.6-1
- Remove unused Doxyfile
- Quiet some autoconf/automake warnings

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 2.5-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 2.4-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 2.4-1
- Merge changes from Mattias Ellert

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 2.3-1
- Version bump for consistency

* Thu Feb 13 2014 Globus Toolkit <support@globus.org> - 2.2-1
- Packaging Fixes

* Tue Jan 21 2014 Globus Toolkit <support@globus.org> - 2.1-1
- Repackage for GT6 without GPT

* Thu Oct 10 2013 Globus Toolkit <support@globus.org> - 1.2-1
- GT-344: Cut and past error in gpt metadata for GRAM LSF module

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 1.1-2
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 1.1-1
- 5.2.3

* Fri Aug 17 2012 Joseph Bester <bester@mcs.anl.gov> - 1.0-1
- Initial packaging
