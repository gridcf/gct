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
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

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
