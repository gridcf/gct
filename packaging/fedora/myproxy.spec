%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:           myproxy
%global soname 6
Version:        6.2.13
Release:        1%{?dist}
Summary:        Manage X.509 Public Key Infrastructure (PKI) security credentials

Group:          Applications/Internet
License:        NCSA and %{?suse_version:BSD-4-clause and BSD-2-clause and Apache-2.0}%{!?suse_version:BSD and ASL 2.0}
URL:            http://grid.ncsa.illinois.edu/myproxy/
Source:         %{name}-%{version}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  gcc
BuildRequires:  globus-common-devel >= 14
BuildRequires:  globus-gssapi-gsi-devel >= 9
BuildRequires:  globus-gss-assist-devel >= 8
BuildRequires:  globus-gsi-sysconfig-devel >= 5
BuildRequires:  globus-gsi-cert-utils-devel >= 8
BuildRequires:  globus-gsi-proxy-core-devel >= 6
BuildRequires:  globus-gsi-credential-devel >= 5
BuildRequires:  globus-gsi-callback-devel >= 4
BuildRequires:  cyrus-sasl-devel
BuildRequires:  krb5-devel
%if %{?suse_version}%{!?suse_version:0}
BuildRequires:  openldap2-devel
%else
BuildRequires:  openldap-devel >= 2.3
%endif
BuildRequires:  pam-devel
%if ! %{?suse_version}%{!?suse_version:0}
BuildRequires:  perl-generators
BuildRequires:  voms-devel >= 1.9.12.1
%endif
%if %{?suse_version}%{!?suse_version:0}
BuildRequires:  insserv
%endif
#               Additional requirements for make check
BuildRequires:  globus-proxy-utils
BuildRequires:  globus-gsi-cert-utils-progs
BuildRequires:  openssl
BuildRequires:  perl-interpreter
BuildRequires:  perl(File::Copy)
BuildRequires:  perl(File::Temp)
BuildRequires:  perl(IPC::Open3)
BuildRequires:  perl(Socket)
%if ! %{?suse_version}%{!?suse_version:0}
BuildRequires:  voms-clients
%endif

%if %{?suse_version}%{!?suse_version:0}
%global libpkg lib%{name}%{soname}
%global nlibpkg -n %{libpkg}
%else
%global libpkg %{name}-libs
%global nlibpkg libs
%endif

Requires:       %{libpkg}%{?_isa} = %{version}-%{release}
Provides:       %{name}-client = %{version}-%{release}
Obsoletes:      %{name}-client < 5.1-3

%description
MyProxy is open source software for managing X.509 Public Key Infrastructure
(PKI) security credentials (certificates and private keys). MyProxy
combines an online credential repository with an online certificate
authority to allow users to securely obtain credentials when and where needed.
Users run myproxy-logon to authenticate and obtain credentials, including
trusted CA certificates and Certificate Revocation Lists (CRLs).

%package %{nlibpkg}
Summary:        Manage X.509 Public Key Infrastructure (PKI) security credentials
Group:          System Environment/Libraries
%if %{?suse_version}%{!?suse_version:0}
Provides:       %{name}-libs = %{version}-%{release}
Obsoletes:      %{name}-libs < %{version}-%{release}
%endif
Requires:       globus-proxy-utils
Obsoletes:      %{name} < 5.1-3

%description %{nlibpkg}
MyProxy is open source software for managing X.509 Public Key Infrastructure
(PKI) security credentials (certificates and private keys). MyProxy
combines an online credential repository with an online certificate
authority to allow users to securely obtain credentials when and where needed.
Users run myproxy-logon to authenticate and obtain credentials, including
trusted CA certificates and Certificate Revocation Lists (CRLs).

Package %{libpkg} contains runtime libs for MyProxy.

%package devel
Summary:        Develop X.509 Public Key Infrastructure (PKI) security credentials
Group:          Development/Libraries
Requires:       %{libpkg}%{?_isa}  = %{version}-%{release}

%description devel
MyProxy is open source software for managing X.509 Public Key Infrastructure
(PKI) security credentials (certificates and private keys). MyProxy
combines an online credential repository with an online certificate
authority to allow users to securely obtain credentials when and where needed.
Users run myproxy-logon to authenticate and obtain credentials, including
trusted CA certificates and Certificate Revocation Lists (CRLs).

Package %{name}-devel contains development files for MyProxy.

%package server
Summary:        Server for X.509 Public Key Infrastructure (PKI) security credentials
Group:          System Environment/Daemons
Requires:       %{libpkg}%{?_isa} = %{version}-%{release}

%if %{?suse_version}%{!?suse_version:0}
Requires(pre):    shadow
Requires(post):   %insserv_prereq  %fillup_prereq
Requires(preun):  %insserv_prereq  %fillup_prereq
Requires(postun): %insserv_prereq  %fillup_prereq
%else
Requires(pre):    shadow-utils
Requires(post):   chkconfig
Requires(preun):  chkconfig
Requires(preun):  initscripts
Requires(postun): initscripts
%endif

%description server
MyProxy is open source software for managing X.509 Public Key Infrastructure
(PKI) security credentials (certificates and private keys). MyProxy
combines an online credential repository with an online certificate
authority to allow users to securely obtain credentials when and where needed.
Users run myproxy-logon to authenticate and obtain credentials, including
trusted CA certificates and Certificate Revocation Lists (CRLs).

Package %{name}-server contains the MyProxy server.

%package admin
# Create a separate admin clients package since they are not needed for normal
# operation and pull in a load of perl dependencies.
Summary:        Server for X.509 Public Key Infrastructure (PKI) security credentials
Group:          Applications/Internet
Requires:       %{libpkg}%{?_isa} = %{version}-%{release}
Requires:       %{name} = %{version}-%{release}
Requires:       %{name}-server = %{version}-%{release}
Requires:       globus-gsi-cert-utils-progs

%description admin
MyProxy is open source software for managing X.509 Public Key Infrastructure
(PKI) security credentials (certificates and private keys). MyProxy
combines an online credential repository with an online certificate
authority to allow users to securely obtain credentials when and where needed.
Users run myproxy-logon to authenticate and obtain credentials, including
trusted CA certificates and Certificate Revocation Lists (CRLs).

Package %{name}-admin contains the MyProxy server admin commands.

%if ! %{?suse_version}%{!?suse_version:0}
%package voms
Summary:        Manage X.509 Public Key Infrastructure (PKI) security credentials
Group:          System Environment/Libraries
Requires:       %{libpkg}%{?_isa} = %{version}-%{release}
Obsoletes:      %{libpkg} < 6.1.6
Requires:       voms-clients

%description voms
MyProxy is open source software for managing X.509 Public Key Infrastructure
(PKI) security credentials (certificates and private keys). MyProxy
combines an online credential repository with an online certificate
authority to allow users to securely obtain credentials when and where needed.
Users run myproxy-logon to authenticate and obtain credentials, including
trusted CA certificates and Certificate Revocation Lists (CRLs).

Package %{name}-voms contains runtime libs for MyProxy to use VOMS.
%endif

%package doc
Summary:        Documentation for X.509 Public Key Infrastructure (PKI) security credentials
Group:          Documentation
BuildArch:      noarch

%description doc
MyProxy is open source software for managing X.509 Public Key Infrastructure
(PKI) security credentials (certificates and private keys). MyProxy
combines an online credential repository with an online certificate
authority to allow users to securely obtain credentials when and where needed.
Users run myproxy-logon to authenticate and obtain credentials, including
trusted CA certificates and Certificate Revocation Lists (CRLs).

Package %{name}-doc contains the MyProxy documentation.

%prep
%setup -q

%build
%configure --disable-static \
           --includedir=%{_includedir}/globus \
           --with-openldap=%{_prefix} \
%if %{?suse_version}%{!?suse_version:0}
           --without-voms \
%else
           --with-voms=%{_prefix} \
%endif
           --with-kerberos5=%{_prefix} \
           --with-sasl2=%{_prefix}

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT

# Remove libtool archives (.la files)
rm $RPM_BUILD_ROOT%{_libdir}/*.la

# No need for myproxy-server-setup since the rpm will perform
# the needed setup
rm $RPM_BUILD_ROOT%{_sbindir}/myproxy-server-setup

# Put documentation in Fedora default location
mkdir -p $RPM_BUILD_ROOT%{_pkgdocdir}/extras
for FILE in login.html myproxy-accepted-credentials-mapapp \
            myproxy-cert-checker myproxy-certificate-mapapp \
            myproxy-certreq-checker myproxy-crl.cron myproxy.cron \
            myproxy-get-delegation.cgi myproxy-get-trustroots.cron \
            myproxy-passphrase-policy myproxy-revoke ; do
   mv $RPM_BUILD_ROOT%{_datadir}/%{name}/$FILE \
      $RPM_BUILD_ROOT%{_pkgdocdir}/extras
done

mkdir -p $RPM_BUILD_ROOT%{_pkgdocdir}
for FILE in LICENSE LICENSE.* PROTOCOL README.sasl REPOSITORY VERSION ; do
   mv $RPM_BUILD_ROOT%{_datadir}/%{name}/$FILE \
      $RPM_BUILD_ROOT%{_pkgdocdir}
done

# Remove irrelevant example configuration files
for FILE in etc.inetd.conf.modifications etc.init.d.myproxy.nonroot \
            etc.services.modifications etc.xinetd.myproxy etc.init.d.myproxy \
            myproxy-server.service myproxy-server.conf ; do
   rm $RPM_BUILD_ROOT%{_datadir}/%{name}/$FILE
done

# Move example configuration file into place
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}
mv $RPM_BUILD_ROOT%{_datadir}/%{name}/myproxy-server.config \
   $RPM_BUILD_ROOT%{_sysconfdir}

mkdir -p $RPM_BUILD_ROOT%{_initddir}
%if %{?suse_version}%{!?suse_version:0}
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/adm/fillup-templates
install -p -m 755 myproxy.init.sles $RPM_BUILD_ROOT%{_initddir}/myproxy-server
install -p -m 644 myproxy.sysconfig \
   $RPM_BUILD_ROOT%{_localstatedir}/adm/fillup-templates/sysconfig.myproxy-server
%else
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/sysconfig
install -p -m 755 myproxy.init $RPM_BUILD_ROOT%{_initddir}/myproxy-server
install -p -m 644 myproxy.sysconfig \
   $RPM_BUILD_ROOT%{_sysconfdir}/sysconfig/myproxy-server
%endif

mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/lib/myproxy

# Create a directory to hold myproxy owned host certificates
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/grid-security/myproxy

%check
make %{?_smp_mflags} check VERBOSE=1

%post %{nlibpkg} -p /sbin/ldconfig

%postun %{nlibpkg} -p /sbin/ldconfig

%pre server
PATH=$PATH:/usr/sbin:/sbin
getent group myproxy > /dev/null || groupadd -r myproxy
getent passwd myproxy > /dev/null || \
useradd -r -g myproxy -d %{_localstatedir}/lib/myproxy -s /sbin/nologin \
   -c "User to run the MyProxy service" myproxy
exit 0

%post server
%if %{?suse_version}%{!?suse_version:0}
%fillup_and_insserv -n myproxy-server myproxy-server
%else
/sbin/chkconfig --add myproxy-server
%endif

%preun server
%if %{?suse_version}%{!?suse_version:0}
%stop_on_removal service
%else
if [ $1 -eq 0 ] ; then
    /sbin/service myproxy-server stop > /dev/null 2>&1 || :
    /sbin/chkconfig --del myproxy-server
fi
%endif

%postun server
%if %{?suse_version}%{!?suse_version:0}
%restart_on_update service
%insserv_cleanup
%else
if [ $1 -ge 1 ] ; then
    /sbin/service myproxy-server condrestart > /dev/null 2>&1 || :
fi
%endif

%files
%defattr(-,root,root,-)
%{_bindir}/myproxy-change-pass-phrase
%{_bindir}/myproxy-destroy
%{_bindir}/myproxy-get-delegation
%{_bindir}/myproxy-get-trustroots
%{_bindir}/myproxy-info
%{_bindir}/myproxy-init
%{_bindir}/myproxy-logon
%{_bindir}/myproxy-retrieve
%{_bindir}/myproxy-store
%{_mandir}/man1/myproxy-change-pass-phrase.1*
%{_mandir}/man1/myproxy-destroy.1*
%{_mandir}/man1/myproxy-get-delegation.1*
%{_mandir}/man1/myproxy-get-trustroots.1*
%{_mandir}/man1/myproxy-info.1*
%{_mandir}/man1/myproxy-init.1*
%{_mandir}/man1/myproxy-logon.1*
%{_mandir}/man1/myproxy-retrieve.1*
%{_mandir}/man1/myproxy-store.1*

%files %{nlibpkg}
%defattr(-,root,root,-)
%{_libdir}/libmyproxy.so.*
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/PROTOCOL
%doc %{_pkgdocdir}/README.sasl
%doc %{_pkgdocdir}/REPOSITORY
%doc %{_pkgdocdir}/VERSION
%doc %{_pkgdocdir}/LICENSE*

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*
%{_libdir}/libmyproxy.so
%{_libdir}/pkgconfig/myproxy.pc

%files server
%defattr(-,root,root,-)
%{_sbindir}/myproxy-server
%{_initddir}/myproxy-server
%if %{?suse_version}%{!?suse_version:0}
%{_localstatedir}/adm/fillup-templates/sysconfig.myproxy-server
%else
%config(noreplace) %{_sysconfdir}/sysconfig/myproxy-server
%endif
%config(noreplace) %{_sysconfdir}/myproxy-server.config
# myproxy-server wants exactly 700 permission on its data
# which is just fine.
%attr(0700,myproxy,myproxy) %dir %{_localstatedir}/lib/myproxy
%dir %{_sysconfdir}/grid-security/myproxy
%{_mandir}/man8/myproxy-server.8*
%{_mandir}/man5/myproxy-server.config.5*
%doc README.Fedora

%files admin
%defattr(-,root,root,-)
%{_sbindir}/myproxy-admin-addservice
%{_sbindir}/myproxy-admin-adduser
%{_sbindir}/myproxy-admin-change-pass
%{_sbindir}/myproxy-admin-load-credential
%{_sbindir}/myproxy-admin-query
%{_sbindir}/myproxy-replicate
%{_sbindir}/myproxy-test
%{_sbindir}/myproxy-test-replicate
%{_mandir}/man8/myproxy-admin-addservice.8*
%{_mandir}/man8/myproxy-admin-adduser.8*
%{_mandir}/man8/myproxy-admin-change-pass.8*
%{_mandir}/man8/myproxy-admin-load-credential.8*
%{_mandir}/man8/myproxy-admin-query.8*
%{_mandir}/man8/myproxy-replicate.8*

%if ! %{?suse_version}%{!?suse_version:0}
%files voms
%defattr(-,root,root,-)
%{_libdir}/libmyproxy_voms.so
%endif

%files doc
%defattr(-,root,root,-)
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/extras
%doc %{_pkgdocdir}/LICENSE*

%changelog
* Sun Apr 10 2022 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.2.13-1
- Use existing socket during trust root retrieval
- Fix wrong name in myproxy-store -V output
- Use write+rename when changing passphrase
- Improve detection of an encrypted private key
- Fix broken snprintf format in test script

* Fri Mar 11 2022 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.2.12-1
- Fix some compiler warnings

* Sun Mar 06 2022 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.2.11-1
- Use sha256 when signing request

* Fri Dec 03 2021 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.2.10-1
- OpenSSL 3.0 compatibility

* Fri Aug 20 2021 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.2.9-1
- Use -l (--listen) flag when starting myproxy-server in test scripts
- Typo fixes

* Thu Mar 04 2021 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.2.8-1
- Update default run directory from /var/run to /run

* Thu Mar 04 2021 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.2.7-1
- Exit with error if voms-proxy-init fails

* Thu Mar 12 2020 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.2.6-2
- Add BuildRequires perl-interpreter
- Add additional perl dependencies for tests

* Sat Jul 13 2019 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.2.6-1
- Remove LICENSE.globus file

* Wed Apr 17 2019 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.2.5-1
- Drop obsolete configure option --with-gpt
- Drop obsolete configure option --with-flavor
- Drop globus_automake_pre and globus_automake_post
- Clean up old GPT references
- Install myproxy-get-trustroots man page

* Fri Dec 07 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.2.4-1
- Remove usage statistics collection support

* Wed Jun 20 2018 Globus Toolkit <support@globus.org> - 6.2.3-1
- remove macro overquoting

* Thu May 17 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.2.2-1
- Use 2048 bit CA key for myproxy tests

* Wed May 02 2018 Globus Toolkit <support@globus.org> - 6.2.1-1
- Fix -Werror=format-security errors

* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 6.2.0-1
- First Grid Community Toolkit release
- Remove support for openssl101e (RHEL5 is EOL)
- Disable usage statistics reporting by default
- Fix option parsing bug

* Mon Jul 10 2017 Globus Toolkit <support@globus.org> - 6.1.28-4
- Remove krb5 dependency on sles.12
- Add /usr/sbin and /sbin for post scripts
- Add shadow to BuildRequires

* Fri May 05 2017 Globus Toolkit <support@globus.org> - 6.1.28-1
- Fix OpenSSL 1.1.0-related typo
- Remove el.5 cruft from spec

* Fri Apr 21 2017 Globus Toolkit <support@globus.org> - 6.1.27-1
- Remove legacy SSLv3 support

* Thu Mar 23 2017 Globus Toolkit <support@globus.org> - 6.1.26-1
- Fix error check

* Tue Jan 10 2017 Globus Toolkit <support@globus.org> - 6.1.25-1
- Don't call ERR_GET_REASON twice #89

* Mon Jan 09 2017 Globus Toolkit <support@globus.org> - 6.1.24-1
- Fix crash in myproxy_bootstrap_trust() with OpenSSL 1.1.0c

* Thu Jan 05 2017 Globus Toolkit <support@globus.org> - 6.1.23-1
- Fixes for OpenSSL 1.1.0
- Reintroduce explicit library dependencies

* Tue Dec 13 2016 Globus Toolkit <support@globus.org> - 6.1.22-1
- Check for openssl 101e for epel5

* Fri Oct 28 2016 Globus Toolkit <support@globus.org> - 6.1.21-2
- Fix naming of dependency

* Mon Sep 19 2016 Globus Toolkit <support@globus.org> - 6.1.21-1
- Do not overwrite configuration flags

* Fri Sep 09 2016 Globus Toolkit <support@globus.org> - 6.1.20-2
- Updates for el.5 with openssl101e

* Tue Sep 06 2016 Globus Toolkit <support@globus.org> - 6.1.19-2
- Fix myproxy dependency

* Wed Aug 31 2016 Globus Toolkit <support@globus.org> - 6.1.19-1
- update myproxy debug/error msgs for accepted_peer_names type change

* Tue Aug 30 2016 Globus Toolkit <support@globus.org> - 6.1.18-5
- Updates for SLES 12

* Tue May 03 2016 Globus Toolkit <support@globus.org> - 6.1.18-1
- Spelling

* Wed Mar 09 2016 Globus Toolkit <support@globus.org> - 6.1.17-1
- Handle error returns from OCSP_parse_url

* Fri Dec 04 2015 Globus Toolkit <support@globus.org> - 6.1.16-1
- Handle invalid proxy_req type

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 6.1.15-1
- Add vendor

* Thu Jul 23 2015 Globus Toolkit <support@globus.org> - 6.1.15-1
- GT-616: Myproxy uses resolved IP address when importing names

* Mon Jun 08 2015 Globus Toolkit <support@globus.org> - 6.1.14-1
- improve rfc2818 name comparison handling

* Tue Apr 07 2015 Globus Toolkit <support@globus.org> - 6.1.13-1
- Fixed 2 instances of underallocation of memory.

* Fri Jan 09 2015 Globus Toolkit <support@globus.org> - 6.1.12-1
- Missing -module

* Mon Dec 22 2014 Globus Toolkit <support@globus.org> - 6.1.11-1
- Fix missing redirect in date detection autoconf

* Tue Dec 16 2014 Globus Toolkit <support@globus.org> - 6.1.10-1
- Fix version and date string macros

* Mon Dec 08 2014 Globus Toolkit <support@globus.org> - 6.1.9-1
- Myproxy systemd fix

* Wed Nov 19 2014 Globus Toolkit <support@globus.org> - 6.1.8-1
- Properly extract MINOR_VERSION from a three digit PACKAGE_VERSION
- Fix undefined symbols in myproxy-voms plugin
- Don't install test wrapper
- Comments are not allowed in tmpfile.d config files

* Tue Nov 18 2014 Globus Toolkit <support@globus.org> - 6.1.7-1
- Allow TLS in myproxy

* Thu Nov 06 2014 Globus Toolkit <support@globus.org> - 6.1.6-3
- Make voms parts optional

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 6.1.5-1
- find paths for cert and proxy utils for tests

* Mon Oct 27 2014 Globus Toolkit <support@globus.org> - 6.1.4-1
- Stop patching myproxy.sysconfig

* Thu Oct 23 2014 Globus Toolkit <support@globus.org> - 6.1.3-1
- Fix incorrect soname change

* Tue Oct 21 2014 Globus Toolkit <support@globus.org> - 6.1.2-1
- Update arg parsing to Getopt::Long

* Tue Oct 21 2014 Globus Toolkit <support@globus.org> - 6.1.1-1
- Increment library age

* Thu Oct 16 2014 Globus Toolkit <support@globus.org> - 6.1-1
- Make sure MAXPATHLEN and PATH_MAX are defined (portability)
- Man page syntax fix
- Propagate version to soname, add missing pkgconfig file, missing dependencies
- fix from ysvenkat: Using command line to pass in the extra long username
- http://myproxy.ncsa.uiuc.edu -> http://grid.ncsa.illinois.edu/myproxy/
- prepare for MyProxy 6.1 release       27e6b38
- documenting git-based procedure as I go       f2664dd
- prepare MyProxy 6.1 release

* Mon Sep 29 2014 Globus Toolkit <support@globus.org> - 6.0-1
- Merge myproxy sources into git repo

* Mon Aug 04 2014 Globus Toolkit <support@globus.org> - 5.10rc3-5
- Quote suse init script

* Fri Aug 01 2014 Globus Toolkit <support@globus.org> - 5.10rc3-4
- Add different init script for suse

* Wed Jul 30 2014 Globus Toolkit <support@globus.org> - 5.10rc3-3
- Add dependency on krb5-devel for SuSE, revert predefining HAVE_GSSAPI_H

* Wed Jul 30 2014 Globus Toolkit <support@globus.org> - 5.10rc3-2
- Remove unused doxygen/LaTeX dependencies

* Wed Jul 30 2014 Globus Toolkit <support@globus.org> - 5.10rc3-1
- Update to myproxy-6.0rc3.tar.gz
- Predefine HAVE_GSSAPI_H on SuSE

* Wed Jul 30 2014 Globus Toolkit <support@globus.org> - 5.10rc2-1
- Update to myproxy-6.0rc2.tar.gz

* Fri Jul 25 2014 Globus Toolkit <support@globus.org> - 5.10rc1-2
- SLES 11 doesn't list chkconfig as a capability

* Thu Jul 24 2014 Globus Toolkit <support@globus.org> - 5.10rc1-1
- Update to 6.0rc1

* Tue Jan 14 2014 Globus Toolkit <support@globus.org> - 5.9-9
- Source0 URL fix

* Wed May 08 2013 Globus Toolkit <support@globus.org> - 5.9-7
- dependency: openldap2-devel for suse

* Fri Mar 15 2013 Globus Toolkit <support@globus.org> - 5.9-6
- Read from /etc/myproxy-server.d when starting the service

* Tue Mar 05 2013 Globus Toolkit <support@globus.org> - 5.9-5
- add missing dependencies

* Tue Mar 05 2013 Globus Toolkit <support@globus.org> - 5.9-4
- Add build dependency on globus-proxy-utils for %%check step

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 5.9-3
- Workaround missing F18 doxygen/latex dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 5.9-2
- 5.2.3

* Wed Jul 25 2012 Joseph Bester <bester@mcs.anl.gov> - 5.9-1
- Fix https://bugzilla.mcs.anl.gov/globus/show_bug.cgi?id=7261

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 5.8-3
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 5.8-2
- GT 5.2.2 Release

* Tue Jun 26 2012 Joseph Bester <bester@mcs.anl.gov> - 5.8-1
- Update to myproxy 5.8 for GT 5.2.2

* Tue May 15 2012 Joseph Bester <bester@mcs.anl.gov> - 5.6-5
- Adjust requirements for SUSE

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 5.6-4
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 5.6-3
- SLES 11 patches

* Wed Feb 29 2012 Joseph Bester <bester@mcs.anl.gov> - 5.6-1
- Updated to MyProxy 5.6

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 5.5-4
- Updated version numbers

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 5.5-3
- Update for 5.2.0 release

* Fri Oct 21 2011 Joseph Bester <bester@mcs.anl.gov> - 5.5-2
- Fix %%post* scripts to check for -eq 1
- Add backward-compatibility aging

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 5.5-1
- Update for 5.1.2 release

* Tue Feb 22 2011 Steve Traylen <steve.traylen@cern.ch> - 5.3-3
- myproxy-vomsc-vomsapi.patch to build against vomsapi rather
  than vomscapi.

* Tue Feb 08 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 5.3-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_15_Mass_Rebuild

* Tue Jan 18 2011 Steve Traylen <steve.traylen@cern.ch> - 5.3-1
- New upstream 5.3.

* Wed Jun 23 2010 Steve Traylen <steve.traylen@cern.ch> - 5.2-1
- New upstream 5.2.
- Drop blocked-signals-with-pthr.patch patch.
* Sat Jun 12 2010 Steve Traylen <steve.traylen@cern.ch> - 5.1-3
- Add blocked-signals-with-pthr.patch patch, rhbz#602594
- Updated init.d script rhbz#603157
- Add myproxy as requires to myproxy-admin to install clients.
* Sat May 15 2010 Steve Traylen <steve.traylen@cern.ch> - 5.1-2
- rhbz#585189 rearrange packaging.
  clients moved from now obsoleted -client package
  to main package.
  libs moved from main package to new libs package.
* Tue Mar 9 2010 Steve Traylen <steve.traylen@cern.ch> - 5.1-1
- New upstream 5.1
- Remove globus-globus-usage-location.patch, now incoperated
  upstream.
* Fri Nov 13 2009 Steve Traylen <steve.traylen@cern.ch> - 4.9-6
- Add requires globus-gsi-cert-utils-progs for grid-proxy-info
  to myproxy-admin package rhbz#536927
- Release bump to F13  so as to be newer than F12.
* Tue Oct 13 2009 Steve Traylen <steve.traylen@cern.ch> - 4.9-3
- Glob on .so.* files to future proof for upgrades.
* Tue Oct 13 2009 Steve Traylen <steve.traylen@cern.ch> - 4.9-1
- New upstream 4.9.
* Tue Oct 13 2009 Steve Traylen <steve.traylen@cern.ch> - 4.8-5
- Disable openldap support for el4 only since openldap to old.
* Wed Oct 7 2009 Steve Traylen <steve.traylen@cern.ch> -  4.8-4
- Add ASL 2.0 license as well.
- Explicitly add /etc/grid-security to files list
- For .el4/5 build only add globus-gss-assist-devel as requirment
  to myproxy-devel package.
* Thu Oct 1 2009 Steve Traylen <steve.traylen@cern.ch> -  4.8-3
- Set _initddir for .el4 and .el5 building.
* Mon Sep 21 2009 Steve Traylen <steve.traylen@cern.ch> -  4.8-2
- Require version of voms with fixed ABI.
* Mon Jun 22 2009 Steve Traylen <steve.traylen@cern.ch> -  4.7-1
- Initial version.

