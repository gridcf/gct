Name:		globus-resource-management-client
%global _name %(tr - _ <<< %{name})
Version:	6.0
Release:	2%{?dist}
Summary:	Grid Community Toolkit - Resource Management Client Programs

Group:		System Environment/Libraries
License:	ASL 2.0
URL:		http://www.globus.org/
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Requires:       globus-common-progs
Requires:       globus-callout
Requires:       globus-gsi-openssl-error
Requires:       globus-gsi-proxy-ssl
Requires:       globus-rsl
Requires:       globus-openssl-module
Requires:       globus-gsi-cert-utils-progs
Requires:       globus-simple-ca
Requires:       globus-gsi-sysconfig
Requires:       globus-gsi-callback
Requires:       globus-gsi-credential
Requires:       globus-gsi-proxy-core
Requires:       globus-gssapi-gsi
Requires:       globus-proxy-utils
Requires:       globus-gss-assist
Requires:       globus-gss-assist-progs
Requires:       globus-xio
Requires:       globus-xio-gsi-driver
Requires:       globus-io
Requires:       globus-gssapi-error
Requires:       globus-gass-transfer
Requires:       globus-gram-protocol
Requires:       globus-gass-server-ez-progs
Requires:       globus-gram-client
Requires:       globus-gram-client-tools

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
Resource Management Client Programs
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
* Wed Aug 26 2015 Joseph Bester <bester@mcs.anl.gov> - 6.0-2
- Remove obsolete globus-core dependency

* Mon Jul 17 2012 Joseph Bester <bester@mcs.anl.gov> - 14.7-3
- GT 5.2.2 New Metapackage
