Name:		globus-gsi
%global _name %(echo %{name} | tr - _)
Version:	6.0
Release:	3%{?dist}
Summary:	Grid Community Toolkit - Security Tools

Group:		System Environment/Libraries
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Requires:	globus-common-progs
Requires:	globus-gsi-cert-utils-progs
Requires:	globus-gss-assist-progs
Requires:	globus-proxy-utils
Requires:	globus-simple-ca

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Security Tools

%prep

%build

%install

%files

%post

%postun

%changelog
* Mon Apr 02 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.0-3
- First Grid Community Toolkit release

* Tue Apr 25 2017 Joseph Bester <bester@mcs.anl.gov> - 6.0-2
- Fix changelog

* Tue Jul 17 2012 Joseph Bester <bester@mcs.anl.gov> - 6.0-1
- GT 5.2.2 New Metapackage
