Name:		globus-gsi
%global _name %(tr - _ <<< %{name})
Version:	6.0
Release:	2%{?dist}
Summary:	Grid Community Toolkit - Security Tools

Group:		System Environment/Libraries
License:	ASL 2.0
URL:		http://www.globus.org/
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Requires:       globus-common-progs
Requires:       globus-gsi-cert-utils-progs
Requires:       globus-gss-assist-progs
Requires:       globus-proxy-utils
Requires:       globus-simple-ca

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
rm -rf "$RPM_BUILD_ROOT"
mkdir "$RPM_BUILD_ROOT"

%files

%clean

%post

%postun

%changelog
* Tue Apr 25 2017 Joseph Bester <bester@mcs.anl.gov> - 6.0-2
- Fix changelog
