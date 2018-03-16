Name:		globus-gram-protocol
%global soname 3
%global _name %(tr - _ <<< %{name})
Version:	12.15
Release:	1%{?dist}
Summary:	Grid Community Toolkit - GRAM Protocol Library

Group:		System Environment/Libraries
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-io-devel >= 8
BuildRequires:	globus-gssapi-gsi-devel >= 10
BuildRequires:	globus-gss-assist-devel >= 8
BuildRequires:	doxygen
%if ! %{?suse_version}%{!?suse_version:0}
BuildRequires:	perl-generators
%endif
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:	automake >= 1.11
BuildRequires:	autoconf >= 2.60
BuildRequires:	libtool >= 2.2
%endif
BuildRequires:	pkgconfig
#		Additional requirements for make check
BuildRequires:	openssl

%if %{?suse_version}%{!?suse_version:0}
%global mainpkg lib%{_name}%{soname}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0}
%package %{?nmainpkg}
Summary:	Grid Community Toolkit - GRAM Protocol Library
Group:		System Environment/Libraries
Provides:	%{name} = %{version}-%{release}
Obsoletes:	%{name} < %{version}-%{release}
%endif

%if %{?suse_version}%{!?suse_version:0}
%{perl_requires}
%else
Requires:	perl(:MODULE_COMPAT_%(eval "`perl -V:version`"; echo $version))
%endif

%package devel
Summary:	Grid Community Toolkit - GRAM Protocol Library Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}

%package doc
Summary:	Grid Community Toolkit - GRAM Protocol Library Documentation Files
Group:		Documentation
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6
BuildArch:	noarch
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0}
%description %{?nmainpkg}
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{mainpkg} package contains:
GRAM Protocol Library
%endif

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
GRAM Protocol Library

%description devel
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-devel package contains:
GRAM Protocol Library Development Files

%description doc
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-doc package contains:
GRAM Protocol Library Documentation Files

%prep
%setup -q -n %{_name}-%{version}

%build
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
# Remove files that should be replaced during bootstrap
rm -rf autom4te.cache

autoreconf -if
%endif

%configure \
	   --disable-static \
	   --docdir=%{_docdir}/%{name}-%{version} \
	   --includedir=%{_includedir}/globus \
	   --libexecdir=%{_datadir}/globus \
	   --with-perlmoduledir=%{perl_vendorlib}

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT

# Remove libtool archives (.la files)
rm $RPM_BUILD_ROOT%{_libdir}/*.la

%check
GLOBUS_HOSTNAME=localhost make %{?_smp_mflags} check VERBOSE=1

%post %{?nmainpkg} -p /sbin/ldconfig

%postun %{?nmainpkg} -p /sbin/ldconfig

%files %{?nmainpkg}
%defattr(-,root,root,-)
%{_libdir}/libglobus_gram_protocol.so.*
%dir %{perl_vendorlib}/Globus
%dir %{perl_vendorlib}/Globus/GRAM
%{perl_vendorlib}/Globus/GRAM/Error.pm
%{perl_vendorlib}/Globus/GRAM/JobSignal.pm
%{perl_vendorlib}/Globus/GRAM/JobState.pm
%dir %{_datadir}/globus
%{_datadir}/globus/globus-gram-protocol-constants.sh
%dir %{_docdir}/%{name}-%{version}
%doc %{_docdir}/%{name}-%{version}/GLOBUS_LICENSE

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*
%{_libdir}/libglobus_gram_protocol.so
%{_libdir}/pkgconfig/%{name}.pc

%files doc
%defattr(-,root,root,-)
%doc %{_mandir}/man3/*
%dir %{_docdir}/%{name}-%{version}
%dir %{_docdir}/%{name}-%{version}/html
%doc %{_docdir}/%{name}-%{version}/html/*
%dir %{_docdir}/%{name}-%{version}/perl
%dir %{_docdir}/%{name}-%{version}/perl/Globus
%dir %{_docdir}/%{name}-%{version}/perl/Globus/GRAM
%doc %{_docdir}/%{name}-%{version}/perl/Globus/GRAM/*
%doc %{_docdir}/%{name}-%{version}/GLOBUS_LICENSE

%changelog
* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 12.15-1
- Update for el.5 openssl101e

* Thu Aug 18 2016 Globus Toolkit <support@globus.org> - 12.14-4
- Updates for SLES 12

* Thu Aug 18 2016 Globus Toolkit <support@globus.org> - 12.14-1
- Makefile fix

* Tue Aug 16 2016 Globus Toolkit <support@globus.org> - 12.13-1
- Updates for OpenSSL 1.1.0

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 12.12-2
- Add vendor

* Tue Sep 23 2014 Globus Toolkit <support@globus.org> - 12.12-1
- Fix some Doxygen issues
- Quiet some autoconf/automake warnings

* Thu Sep 18 2014 Globus Toolkit <support@globus.org> - 12.11-1
- GT-455: Incorporate OSG patches
- GT-461: OSG patch "increase-concurrency.patch" for globus-gram-protocol

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 12.10-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 12.9-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 12.9-1
- Merge changes from Mattias Ellert

* Mon Apr 21 2014 Globus Toolkit <support@globus.org> - 12.8-1
- Test fixes

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 12.7-1
- Version bump for consistency

* Thu Feb 27 2014 Globus Toolkit <support@globus.org> - 12.6-1
- Packaging fixes, Warning Cleanup

* Mon Feb 24 2014 Globus Toolkit <support@globus.org> - 12.5-1
- Test fixes

* Thu Feb 13 2014 Globus Toolkit <support@globus.org> - 12.4-1
- Test fixes

* Thu Feb 13 2014 Globus Toolkit <support@globus.org> - 12.3-1
- Test fixes

* Mon Feb 03 2014 Globus Toolkit <support@globus.org> - 12.2-1
- Test credentials permissions

* Mon Feb 03 2014 Globus Toolkit <support@globus.org> - 12.1-1
- Dependency

* Tue Jan 21 2014 Globus Toolkit <support@globus.org> - 12.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 11.3-8
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 11.3-7
- Workaround missing F18 doxygen/latex dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 11.3-6
- 5.2.3

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 11.3-5
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 11.3-4
- GT 5.2.2 Release

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 11.3-3
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 11.3-2
- SLES 11 patches

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 11.3-1
- RIC-226: Some dependencies are missing in GPT metadata

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 11.2-3
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 11.2-2
- Last sync prior to 5.2.0

* Tue Nov 15 2011 Joseph Bester <bester@mcs.anl.gov> - 11.2-1
- Enable IPv6 Support

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 11.1-2
- Add explicit dependencies on >= 5.2 libraries

* Thu Oct 06 2011 Joseph Bester <bester@mcs.anl.gov> - 11.1-1
- Add backward-compatibility aging

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 11.0-2
- Update for 5.1.2 release

* Sun Jun 05 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 9.7-6
- Fix doxygen markup

* Mon Apr 25 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 9.7-5
- Add README file

* Tue Feb 08 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 9.7-4
- Rebuilt for https://fedoraproject.org/wiki/Fedora_15_Mass_Rebuild

* Sat Jul 17 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 9.7-3
- Simplify requirements - no longer building on RHEL3

* Tue Jun 01 2010 Marcela Maslanova <mmaslano@redhat.com> - 9.7-2
- Mass rebuild with perl-5.12.0

* Wed Apr 14 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 9.7-1
- Update to Globus Toolkit 5.0.1
- Drop patches globus-gram-protocol-dep.patch and
  globus-gram-protocol-typo.patch (fixed upstream)

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 9.3-1
- Update to Globus Toolkit 5.0.0

* Wed Jul 29 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 7.4-1
- Autogenerated
