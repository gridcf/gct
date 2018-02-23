%{!?perl_vendorlib: %global perl_vendorlib %(eval "`perl -V:installvendorlib`"; echo $installvendorlib)}

Name:		globus-gram-job-manager-condor
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	2.6
Release:	6%{?dist}
Summary:	Globus Toolkit - Condor Job Manager

Group:		Applications/Internet
License:	%{apache_license}
URL:		http://toolkit.globus.org/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
Obsoletes:      globus-gram-job-manager-setup-condor < 4.5
Requires:	globus-gram-job-manager-scripts >= 3.4
Requires:	globus-gass-cache-program >= 2
Requires:	globus-common-progs >= 2
%if 0%{?suse_version} > 0
    %if %{suse_version} < 1140
Requires:     perl = %{perl_version}
    %else
%{perl_requires}
    %endif
%else
Requires:	perl(:MODULE_COMPAT_%(eval "`perl -V:version`"; echo $version))
%endif
Requires(post): globus-gram-job-manager-scripts >= 4
Requires(preun): globus-gram-job-manager-scripts >= 4
Provides:       globus-gram-job-manager-setup
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  automake >= 1.11
BuildRequires:  autoconf >= 2.60
BuildRequires:  libtool >= 2.2
%endif
BuildRequires:  pkgconfig
BuildArch:      noarch

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
Condor Job Manager 

%prep
%setup -q -n %{_name}-%{version}

%build
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
# Remove files that should be replaced during bootstrap
rm -rf autom4te.cache

autoreconf -if
%endif

export CONDOR_RM=/usr/bin/condor_rm
export CONDOR_SUBMIT=/usr/bin/condor_submit

%configure \
           --disable-static \
           --docdir=%{_docdir}/%{name}-%{version} \
           --includedir=%{_includedir}/globus \
           --libexecdir=%{_datadir}/globus \
           --with-perlmoduledir=%{perl_vendorlib}

make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
# Remove jobmanager-condor from install dir so that it can be
# added/removed by post scripts
rm $RPM_BUILD_ROOT/etc/grid-services/jobmanager-condor

%clean
rm -rf $RPM_BUILD_ROOT

%post
if [ $1 -eq 1 ]; then
    globus-gatekeeper-admin -e jobmanager-condor > /dev/null 2>&1 || :
    if [ ! -f /etc/grid-services/jobmanager ]; then
        globus-gatekeeper-admin -e jobmanager-condor -n jobmanager || :
    fi
fi

%preun
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-condor > /dev/null 2>&1 || :
fi

%postun
if [ $1 -eq 0 -a ! -f /etc/grid-services/jobmanager ]; then
    globus-gatekeeper-admin -E > /dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}
%{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%dir %{_sysconfdir}/grid-services
%dir %{_sysconfdir}/grid-services/available
%dir %{_sysconfdir}/globus
%dir %{perl_vendorlib}/Globus
%dir %{perl_vendorlib}/Globus/GRAM
%dir %{perl_vendorlib}/Globus/GRAM/JobManager
%dir %{_datadir}/globus
%dir %{_datadir}/globus/globus_gram_job_manager
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-condor
%config(noreplace) %{_sysconfdir}/globus/globus-condor.conf
%{perl_vendorlib}/Globus/GRAM/JobManager/condor.pm
%{_datadir}/globus/globus_gram_job_manager/condor.rvf

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

