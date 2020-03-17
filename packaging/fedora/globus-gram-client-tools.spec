%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-gram-client-tools
%global soname 0
%global _name %(echo %{name} | tr - _)
Version:	12.1
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Job Management Tools (globusrun)

Group:		Applications/Internet
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	gcc
BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-gram-client-devel >= 12
BuildRequires:	globus-gram-protocol-devel >= 11
BuildRequires:	globus-gass-transfer-devel >= 7
BuildRequires:	globus-gass-server-ez-devel >= 4
BuildRequires:	globus-rsl-devel >= 9
BuildRequires:	globus-gss-assist-devel >= 8

Requires:	globus-common-progs >= 14

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Job Management Tools (globusrun)

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

%files
%defattr(-,root,root,-)
%{_bindir}/globus-job-cancel
%{_bindir}/globus-job-clean
%{_bindir}/globus-job-get-output
%{_bindir}/globus-job-get-output-helper
%{_bindir}/globus-job-run
%{_bindir}/globus-job-status
%{_bindir}/globus-job-submit
%{_bindir}/globusrun
%doc %{_mandir}/man1/globus-job-cancel.1*
%doc %{_mandir}/man1/globus-job-clean.1*
%doc %{_mandir}/man1/globus-job-get-output.1*
%doc %{_mandir}/man1/globus-job-get-output-helper.1*
%doc %{_mandir}/man1/globus-job-run.1*
%doc %{_mandir}/man1/globus-job-status.1*
%doc %{_mandir}/man1/globus-job-submit.1*
%doc %{_mandir}/man1/globusrun.1*
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%changelog
* Mon Mar 16 2020 Mattias Ellert <mattias.ellert@physics.uu.se> - 12.1-1
- Install globus-job-get-output-helper man page

* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 12.0-1
- First Grid Community Toolkit release

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 11.10-1
- Update for el.5 openssl101e, replace docbook with asciidoc

* Tue Aug 30 2016 Globus Toolkit <support@globus.org> - 11.9-2
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 11.9-1
- Update bug report URL

* Tue May 03 2016 Globus Toolkit <support@globus.org> - 11.8-1
- Spelling

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 11.7-2
- Add vendor

* Tue Sep 30 2014 Globus Toolkit <support@globus.org> - 11.7-1
- Add missing asciidoc manpage source

* Thu Sep 25 2014 Globus Toolkit <support@globus.org> - 11.6-1
- Convert manpage sources into asciidoc, fix errors and typos

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 11.5-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 11.4-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 11.4-1
- Merge changes from Mattias Ellert

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 11.3-1
- Packaging fixes

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 11.2-1
- Version bump for consistency

* Thu Feb 27 2014 Globus Toolkit <support@globus.org> - 11.1-1
- Packaging fixes, Warning Cleanup

* Thu Jan 23 2014 Globus Toolkit <support@globus.org> - 11.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 10.4-5
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 10.4-4
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 10.4-3
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 10.4-2
- GT 5.2.2 Release

* Mon May 21 2012 Joseph Bester <bester@mcs.anl.gov> - 10.4-1
- GT-198: globusrun crashes when authentication fails for status check

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 10.3-2
- RHEL 4 patches

* Wed Apr 11 2012 Joseph Bester <bester@mcs.anl.gov> - 10.3-1
- GRAM-339: globus-job-run and globus-job-submit can't always handle "-e" as an argument

* Wed Apr 11 2012 Joseph Bester <bester@mcs.anl.gov> - 10.2-1
- GRAM-331: Remove dead code from globusrun
- GRAM-341: globusrun ignores state callbacks that occur too early

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 10.1-1
- GRAM-311: Undefined variable defaults in shell scripts
- RIC-226: Some dependencies are missing in GPT metadata

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 10.0-5
- Update for 5.2.0 release

* Mon Nov 21 2011 Joseph Bester <bester@mcs.anl.gov> - 10.0-4
- GRAM-281: Missing dependency in globus-gram-client-tools RPM

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 10.0-3
- Add explicit dependencies on >= 5.2 libraries

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 10.0-2
- Update for 5.1.2 release

* Wed Aug 31 2011 Joseph Bester <bester@mcs.anl.gov> - 10.0-1
- Updated version numbers

* Mon Apr 25 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 8.2-3
- Add README file

* Tue Feb 08 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 8.2-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_15_Mass_Rebuild

* Sat Jul 17 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 8.2-1
- Update to Globus Toolkit 5.0.2

* Wed Apr 14 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 8.1-1
- Update to Globus Toolkit 5.0.1
- Drop patch globus-gram-client-tools.patch (fixed upstream)

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 7.3-1
- Update to Globus Toolkit 5.0.0

* Tue Jul 28 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 5.0-1
- Autogenerated
