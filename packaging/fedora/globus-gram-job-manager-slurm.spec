Name:		globus-gram-job-manager-slurm
%global _name %(tr - _ <<< %{name})
Version:	2.8
Release:	3%{?dist}
Summary:	Grid Community Toolkit - SLURM Job Manager Support

Group:		Applications/Internet
#		The slurm.pm file is BSD, the rest is ASL 2.0
License:	%{?suse_version:Apache-2.0 and BSD-2-clause}%{!?suse_version:ASL 2.0 and BSD}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildArch:	noarch

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
Obsoletes:	%{name}-setup-poll < 2.9

Requires(preun):	globus-gram-job-manager-scripts >= 4

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
SLURM Job Manager Support

%prep
%setup -q -n %{_name}-%{version}

%build
export MPIRUN=no
export SRUN=%{_bindir}/srun
export SBATCH=%{_bindir}/sbatch
export SALLOC=%{_bindir}/salloc
export SCANCEL=%{_bindir}/scancel
export SCONTROL=%{_bindir}/scontrol
%configure \
	   --disable-static \
	   --docdir=%{_docdir}/%{name}-%{version} \
	   --includedir=%{_includedir}/globus \
	   --libexecdir=%{_datadir}/globus \
	   --with-perlmoduledir=%{perl_vendorlib}

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT

# Remove jobmanager-slurm from install dir - leave it for admin configuration
rm $RPM_BUILD_ROOT%{_sysconfdir}/grid-services/jobmanager-slurm

%preun
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-slurm-poll > /dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%{_datadir}/globus/globus_gram_job_manager/slurm.rvf
%dir %{perl_vendorlib}/Globus
%dir %{perl_vendorlib}/Globus/GRAM
%dir %{perl_vendorlib}/Globus/GRAM/JobManager
%{perl_vendorlib}/Globus/GRAM/JobManager/slurm.pm
%config(noreplace) %{_sysconfdir}/globus/globus-slurm.conf
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-slurm-poll
%dir %{_docdir}/%{name}-%{version}
%doc %{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%doc %{_docdir}/%{name}-%{version}/LICENSE*

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
