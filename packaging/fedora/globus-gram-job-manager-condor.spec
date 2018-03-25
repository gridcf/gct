%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-gram-job-manager-condor
%global _name %(tr - _ <<< %{name})
Version:	2.6
Release:	6%{?dist}
Summary:	Grid Community Toolkit - Condor Job Manager Support

Group:		Applications/Internet
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
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
Provides:	globus-gram-job-manager-setup-condor = 4.5
Obsoletes:	globus-gram-job-manager-setup-condor < 4.5
Obsoletes:	globus-gram-job-manager-setup-condor-doc < 4.5

Requires(preun):	globus-gram-job-manager-scripts >= 4

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Condor Job Manager Support

%prep
%setup -q -n %{_name}-%{version}

%build
export CONDOR_RM=%{_bindir}/condor_rm
export CONDOR_SUBMIT=%{_bindir}/condor_submit
%configure --disable-static \
	   --includedir=%{_includedir}/globus \
	   --libexecdir=%{_datadir}/globus \
	   --docdir=%{_pkgdocdir} \
	   --with-perlmoduledir=%{perl_vendorlib} \
	   --with-condor-os=LINUX \
	   --with-condor-arch=""

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT

# Remove jobmanager-condor from install dir - leave it for admin configuration
rm $RPM_BUILD_ROOT%{_sysconfdir}/grid-services/jobmanager-condor

%preun
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-condor > /dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%{_datadir}/globus/globus_gram_job_manager/condor.rvf
%dir %{perl_vendorlib}/Globus
%dir %{perl_vendorlib}/Globus/GRAM
%dir %{perl_vendorlib}/Globus/GRAM/JobManager
%{perl_vendorlib}/Globus/GRAM/JobManager/condor.pm
%config(noreplace) %{_sysconfdir}/globus/globus-condor.conf
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-condor
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%changelog
* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 2.6-6
- Rebuild after changes for el.5 with openssl101e

* Mon Aug 29 2016 Globus Toolkit <support@globus.org> - 2.6-5
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 2.6-1
- Update bug report URL

* Thu Oct 22 2015 Globus Toolkit <support@globus.org> - 2.5-3
- BuildArch: noarch

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 2.5-2
- Add vendor

* Thu Sep 18 2014 Globus Toolkit <support@globus.org> - 2.5-1
- GT-455: Incorporate OSG patches
- GT-457: OSG patch "nfslite.patch" for globus-gram-job-manager-condor
- GT-458: OSG patch "groupacct.patch" for globus-gram-job-manager-condor
- GT-459: OSG patch "669-xcount.patch" for globus-gram-job-manager-condor
- GT-460: OSG patch "717-max-walltime.patch" for globus-gram-job-manager-condor

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 2.4-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 2.3-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 2.3-1
- Merge changes from Mattias Ellert

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 2.2-1
- Version bump for consistency

* Thu Feb 20 2014 Globus Toolkit <support@globus.org> - 2.1-1
- Packaging fixes

* Wed Jan 22 2014 Globus Toolkit <support@globus.org> - 2.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 1.4-4
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 1.4-3
- Workaround missing F18 doxygen/latex dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 1.4-2
- 5.2.3

* Wed Sep 12 2012 Joseph Bester <bester@mcs.anl.gov> - 1.4-1
- globus bugzilla #5143: DONE state never reported for Condor jobs when using Condor-G grid monitor

* Wed Aug 15 2012 Joe Bester <jbester@mactop2.local> - 1.3-6
- GT-267: /etc/globus/globus-condor.conf is not marked as a config file in RPM spec

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 1.3-5
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 1.3-4
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 1.3-3
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 1.3-2
- SLES 11 patches

* Thu Apr 12 2012 Joseph Bester <bester@mcs.anl.gov> - 1.3-1
- GRAM-343: lrm packages grid-service files aren't in CLEANFILES

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 1.2-1
- GRAM-297: job manager service definitions contain unresolved variables
- GRAM-310: sge configure script error
- RIC-229: Clean up GPT metadata

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 1.0-6
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 1.0-5
- Last sync prior to 5.2.0

* Thu Oct 20 2011 Joseph Bester <bester@mcs.anl.gov> - 1.0-4
- GRAM-259: globus-gram-job-manager-condor RPM does not uninstall cleanly
- Add explicit dependencies on >= 5.2 libraries

* Thu Sep 22 2011 Joseph Bester <bester@mcs.anl.gov> - 1.0-3
- Fix: GRAM-243

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 1.0-2
- Update for 5.1.2 release

