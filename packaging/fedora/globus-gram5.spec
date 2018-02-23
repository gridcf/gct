Name:		globus-gram5
%global _name %(tr - _ <<< %{name})
Version:	6.0
Release:	1%{?dist}
Summary:	Globus Toolkit - GRAM5 Bundle

Group:		System Environment/Libraries
License:	ASL 2.0
URL:		http://www.globus.org/
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Requires:       globus-gatekeeper
Requires:       globus-gram-job-manager
Requires:       globus-gram-job-manager-scripts
Requires:       globus-gram-job-manager-fork-setup-poll
Requires:       globus-gram-client-tools
Requires:       globus-gass-cache-program
Requires:       globus-gass-server-ez-progs
Requires:       globus-gss-assist-progs
Requires:       globus-common-progs
Requires:       globus-gsi-cert-utils-progs
Requires:       globus-proxy-utils

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
GRAM5 Client and Server Programs and Libraries
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
* Mon Jul 17 2012 Joseph Bester <bester@mcs.anl.gov> - 14.7-3
- GT 5.2.2 New Metapackage
