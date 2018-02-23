%{!?perl_vendorlib: %global perl_vendorlib %(eval "`perl -V:installvendorlib`"; echo $installvendorlib)}

Name:		globus-gram-job-manager-slurm
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	2.8
Release:	3%{?dist}
Summary:	Globus Toolkit - SLURM Job Manager

Group:		Applications/Internet
License:	%{apache_license}
URL:		http://toolkit.globus.org/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Requires:       globus-gram-job-manager-scripts >= 5
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
BuildRequires:	globus-gram-protocol-devel >= 11
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  automake >= 1.11
BuildRequires:  autoconf >= 2.60
BuildRequires:  libtool >= 2.2
%endif
BuildArch:      noarch

%package setup-poll
Summary:        Globus Toolkit - SLURM Job Manager Setup Files
Group:		Applications/Internet
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6
BuildArch:      noarch
%endif
Provides:       %{name}-setup
Provides:       globus-gram-job-manager-setup
Requires:	%{name} = %{version}-%{release}
requires(post): globus-gram-job-manager-scripts >= 5
requires(preun): globus-gram-job-manager-scripts >= 5
Conflicts:      %{name}-setup-seg

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
SLURM Job Manager 

%description setup-poll
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
SLURM Job Manager Setup using polling to monitor job state

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

%configure \
           --disable-static \
           --docdir=%{_docdir}/%{name}-%{version} \
           --includedir=%{_includedir}/globus \
           --libexecdir=%{_datadir}/globus \
           --with-globus-state-dir=%{_localstatedir}/lib/globus \
           --with-perlmoduledir=%{perl_vendorlib}

make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
rm -rf $RPM_BUILD_ROOT/etc/grid-services/jobmanager-slurm

%check
make %{_smp_mflags} check

%clean
rm -rf $RPM_BUILD_ROOT

%post setup-poll
if [ $1 -eq 1 ]; then
    globus-gatekeeper-admin -e jobmanager-slurm-poll -n jobmanager-slurm > /dev/null 2>&1 || :
    if [ ! -f /etc/grid-services/jobmanager ]; then
        globus-gatekeeper-admin -e jobmanager-slurm-poll -n jobmanager
    fi
fi

%preun setup-poll
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-slurm-poll > /dev/null 2>&1 || :
fi

%postun setup-poll
if [ $1 -eq 1 ]; then
    globus-gatekeeper-admin -e jobmanager-slurm-poll -n jobmanager-slurm > /dev/null 2>&1 || :
elif [ $1 -eq 0 -a ! -f /etc/grid-services/jobmanager ]; then
    globus-gatekeeper-admin -E > /dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%dir %{perl_vendorlib}/Globus/GRAM/JobManager
%{perl_vendorlib}/Globus/GRAM/JobManager/slurm.pm
%dir %{_docdir}/%{name}-%{version}
%{_docdir}/%{name}-%{version}/*LICENSE*
%dir %{_sysconfdir}/globus
%config(noreplace) %{_sysconfdir}/globus/globus-slurm.conf
%dir %{_datadir}/globus/globus_gram_job_manager
%{_datadir}/globus/globus_gram_job_manager/slurm.rvf


%files setup-poll
%defattr(-,root,root,-)
%dir %{_sysconfdir}/grid-services
%dir %{_sysconfdir}/grid-services/available
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-slurm-poll

%changelog
* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 2.8-3
- Rebuild after changes for el.5 with openssl101e

* Mon Aug 29 2016 Globus Toolkit <support@globus.org> - 2.8-2
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 2.8-1
- Update bug report URL

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 2.7-2
- Add vendor

* Wed Jul 01 2015 Globus Toolkit <support@globus.org> - 2.7-1
- GT-609: Add job_dependency RSL to SLURM LRM

* Wed May 06 2015 Globus Toolkit <support@globus.org> - 2.6-1
- GT-595: Remove GRAM slurm option: SBATCH -l h_cpu

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 2.5-1
- doxygen fixes

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 2.4-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 2.3-3
- Fix Source path

* Wed Jun 25 2014 Globus Toolkit <support@globus.org> - 2.3-2
- Remove .pc file from package

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 2.3-1
- Merge changes from Mattias Ellert

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 2.2-1
- Version bump for consistency

* Sat Feb 15 2014 Globus Toolkit <support@globus.org> - 2.1-1
- Packaging fixes

* Wed Jan 22 2014 Globus Toolkit <support@globus.org> - 2.0-1
- Repackage for GT6 without GPT

* Mon Oct 28 2013 Globus Toolkit <support@globus.org> - 1.2-1
- update description

* Tue Sep 17 2013 Globus Toolkit <support@globus.org> - 1.1-1
- Search for commands in path if not in config

* Mon Sep 09 2013 Globus Toolkit <support@globus.org> - 1.0-1
- Initial packaging of SLURM LRM
