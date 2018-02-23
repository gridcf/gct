%{!?perl_vendorlib: %global perl_vendorlib %(eval "`perl -V:installvendorlib`"; echo $installvendorlib)}

Name:		globus-gram-audit
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	4.6
Release:	1%{?dist}
Summary:	Grid Community Toolkit - GRAM Auditing

Group:		Applications/Internet
License:	%{apache_license}
URL:		http://toolkit.globus.org/
Source:	http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildArch:      noarch
%endif
%if 0%{?suse_version} > 0
    %if %{suse_version} < 1140
Requires:     perl = %{perl_version}
    %else
%{perl_requires}
    %endif
%else
Requires:	perl(:MODULE_COMPAT_%(eval "`perl -V:version`"; echo $version))
%endif
Requires:	perl(DBI)
%if 0%{?suse_version} == 0
Requires:	crontabs
%endif
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:	automake >= 1.11
BuildRequires:	autoconf >= 2.60
BuildRequires:	libtool >= 2.2
%endif
%if %{?suse_version}%{!?suse_version:0} >= 1315
Recommends:     cron
%endif

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
GRAM Auditing

%prep
%setup -q -n %{_name}-%{version}

%build
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7
# Remove files that should be replaced during bootstrap
rm -rf autom4te.cache

autoreconf -if
%endif


%configure \
           --disable-static \
           --docdir=%{_docdir}/%{name}-%{version} \
           --includedir=%{_includedir}/globus \
           --libexecdir=%{_datadir}/globus

make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

%if %{?suse_version}%{!?suse_version:0} >= 1315
rm -rf $RPM_BUILD_ROOT%{_localstatedir}/lib/globus/gram-audit
%endif

%clean
rm -rf $RPM_BUILD_ROOT

%post
if [ $1 -eq 1 ]; then
    globus-gram-audit --query 'select 1 from gram_audit_table' 2> /dev/null \
    || globus-gram-audit --create --quiet \
    || :
fi
%if %{?suse_version}%{!?suse_version:0} >= 1315
mkdir -p %{_localstatedir}/lib/globus
mkdir -m 01733 -p %{_localstatedir}/lib/globus/gram-audit
%endif

%files
%defattr(-,root,root,-)
%dir %{_localstatedir}/lib/globus
%if %{?suse_version}%{!?suse_version:0} < 1315
%dir %{_localstatedir}/lib/globus/gram-audit
%endif
%dir %{_docdir}/%{name}-%{version}
%{_sbindir}/globus-gram-audit
%{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%dir %{_datadir}/globus
%dir %{_datadir}/globus/gram-audit
%{_datadir}/globus/gram-audit/*
%{_mandir}/man8/*
%config(noreplace) %{_sysconfdir}/cron.hourly/globus-gram-audit.cron
%dir %{_sysconfdir}/globus
%config(noreplace) %{_sysconfdir}/globus/gram-audit.conf

%changelog
* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 4.6-1
- Update for el.5 openssl101e, replace docbook with asciidoc

* Mon Aug 29 2016 Globus Toolkit <support@globus.org> - 4.5-7
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 4.5-1
- Update bug report URL

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 4.4-2
- Add vendor

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 4.4-1
- doxygen fixes

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 4.3-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 4.2-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 4.2-1
- Merge changes from Mattias Ellert

* Thu Apr 24 2014 Globus Toolkit <support@globus.org> - 4.1-1
- Packaging fixes

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 3.2-5
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 3.2-4
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 3.2-3
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 3.2-2
- GT 5.2.2 Release

* Mon Jun 25 2012 Joe Bester <bester@mcs.anl.gov> - 3.2-1
- GT-236: gram audit makefile has missing parameter to mkdir

* Tue May 15 2012 Joseph Bester <bester@mcs.anl.gov> - 3.1-8
- Adjust requirements for SUSE

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 3.1-7
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 3.1-6
- SLES 11 patches

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 3.1-5
- GRAM-312: Make crontab not fail if the package is uninstalled

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 3.1-4
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 3.1-3
- Last sync prior to 5.2.0

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 3.1-2
- Add explicit dependencies on >= 5.2 libraries

* Fri Sep 02 2011 Joseph Bester <bester@mcs.anl.gov> - 3.1-2
- Fix incorrect path to globus-gram-job-manager.conf

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 3.0-2
- Update for 5.1.2 release

* Wed Aug 31 2011 Joseph Bester <bester@mcs.anl.gov> - 3.0-1
- Updated version numbers

