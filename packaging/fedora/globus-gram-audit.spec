%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-gram-audit
%global _name %(tr - _ <<< %{name})
Version:	5.0
Release:	1%{?dist}
Summary:	Grid Community Toolkit - GRAM Jobmanager Auditing

Group:		Applications/Internet
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildArch:	noarch

%if ! %{?suse_version}%{!?suse_version:0}
BuildRequires:	perl-generators
%endif

%if %{?suse_version}%{!?suse_version:0}
Recommends:	cron
%else
Requires:	crontabs
%endif
Requires:	perl(DBD::SQLite)

Requires(post):	perl(DBD::SQLite)
Requires(post):	perl(Globus::Core::Paths)

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
GRAM Jobmanager Auditing

%prep
%setup -q -n %{_name}-%{version}

%build
%configure --disable-static \
	   --includedir=%{_includedir}/globus \
	   --libexecdir=%{_datadir}/globus \
	   --docdir=%{_pkgdocdir}

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT

# Rename cron script
mv $RPM_BUILD_ROOT%{_sysconfdir}/cron.hourly/globus-gram-audit.cron \
   $RPM_BUILD_ROOT%{_sysconfdir}/cron.hourly/globus-gram-audit

%post
if [ $1 -eq 1 ]; then
    globus-gram-audit --query 'select 1 from gram_audit_table' 2> /dev/null || \
    globus-gram-audit --create --quiet || :
fi

%files
%defattr(-,root,root,-)
%{_sbindir}/globus-gram-audit
%dir %{_datadir}/globus
%dir %{_datadir}/globus/gram-audit
%{_datadir}/globus/gram-audit/*.sql
%dir %{_localstatedir}/lib/globus
%dir %{_localstatedir}/lib/globus/gram-audit
%config(noreplace) %{_sysconfdir}/cron.hourly/globus-gram-audit
%dir %{_sysconfdir}/globus
%config(noreplace) %{_sysconfdir}/globus/gram-audit.conf
%doc %{_mandir}/man8/globus-gram-audit.8*
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%changelog
* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 5.0-1
- First Grid Community Toolkit release
- Add Requires on perl(DBD::SQLite)

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

