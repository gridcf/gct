Name:		globus-resource-management-client
%global _name %(echo %{name} | tr - _)
Version:	6.0
Release:	3%{?dist}
Summary:	Grid Community Toolkit - Resource Management Client Programs

Group:		System Environment/Libraries
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Requires:	globus-common-progs
Requires:	globus-callout
Requires:	globus-gsi-openssl-error
Requires:	globus-gsi-proxy-ssl
Requires:	globus-rsl
Requires:	globus-openssl-module
Requires:	globus-gsi-cert-utils-progs
Requires:	globus-simple-ca
Requires:	globus-gsi-sysconfig
Requires:	globus-gsi-callback
Requires:	globus-gsi-credential
Requires:	globus-gsi-proxy-core
Requires:	globus-gssapi-gsi
Requires:	globus-proxy-utils
Requires:	globus-gss-assist
Requires:	globus-gss-assist-progs
Requires:	globus-xio
Requires:	globus-xio-gsi-driver
Requires:	globus-io
Requires:	globus-gssapi-error
Requires:	globus-gass-transfer
Requires:	globus-gram-protocol
Requires:	globus-gass-server-ez-progs
Requires:	globus-gram-client
Requires:	globus-gram-client-tools

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Resource Management Client Programs
%prep

%build

%install

%files

%post

%postun

%changelog
* Mon Apr 02 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.0-3
- First Grid Community Toolkit release

* Wed Aug 26 2015 Joseph Bester <bester@mcs.anl.gov> - 6.0-2
- Remove obsolete globus-core dependency

* Tue Jul 17 2012 Joseph Bester <bester@mcs.anl.gov> - 6.0-1
- GT 5.2.2 New Metapackage
