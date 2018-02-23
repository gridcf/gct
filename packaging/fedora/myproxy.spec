%{!?_initddir: %global _initddir %{_initrddir}}
Name:           myproxy
%global soname 6
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%global bsd_license BSD-4-Clause
%global libpkg libmyproxy%{soname}
%global nlibpkg -n libmyproxy%{soname}
%else
%global apache_license ASL 2.0
%global bsd_license BSD
%global libpkg  myproxy-libs
%global nlibpkg libs
%endif
%global _name %(tr - _ <<< %{name})
Version:	6.1.28
Release:	4%{?dist}
Summary:        Manage X.509 Public Key Infrastructure (PKI) security credentials

Group:          System Environment/Daemons
License:        NCSA and %{bsd_license} and %{apache_license}
URL:            http://grid.ncsa.illinois.edu/myproxy/
Source0:        http://toolkit.globus.org/ftppub/gt6/packages/%{_name}-%{version}.tar.gz

BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  openssl

%if %{?suse_version}%{!?suse_version:0} >= 1315
Requires:       insserv
Requires(post): %insserv_prereq  %fillup_prereq
BuildRequires:  insserv
BuildRequires:  shadow
%endif

BuildRequires:  globus-gss-assist-devel >= 8
BuildRequires:  globus-usage-devel >= 3
BuildRequires:  pam-devel

%if %{?fedora}%{!?fedora:0} > 0 || %{?rhel}%{!?rhel:0} > 5
BuildRequires:  voms-devel >= 1.9.12.1
%endif

BuildRequires:  cyrus-sasl-devel

%if %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  openldap2-devel
%else
BuildRequires:  openldap-devel >= 2.3
%endif

BuildRequires:      globus-proxy-utils >= 5
BuildRequires:      globus-gsi-cert-utils-progs >= 8
BuildRequires:      globus-common-devel >= 14
BuildRequires:      globus-xio-devel >= 3
BuildRequires:      globus-usage-devel >= 3
BuildRequires:      globus-gss-assist-devel >= 8

Requires:      globus-proxy-utils >= 5
Requires:      %{libpkg}%{?_isa} = %{version}-%{release}
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  automake >= 1.11
BuildRequires:  autoconf >= 2.60
BuildRequires:  libtool >= 2.2
%endif
BuildRequires:  pkgconfig

Obsoletes:     myproxy-client < 5.1-3
Provides:      myproxy-client = %{version}-%{release}

%description
MyProxy is open source software for managing X.509 Public Key Infrastructure 
(PKI) security credentials (certificates and private keys). MyProxy 
combines an online credential repository with an online certificate 
authority to allow users to securely obtain credentials when and where needed.
Users run myproxy-logon to authenticate and obtain credentials, including 
trusted CA certificates and Certificate Revocation Lists (CRLs). 

%package %{nlibpkg}
Summary:       Manage X.509 Public Key Infrastructure (PKI) security credentials 
Group:         System Environment/Daemons

%description %{nlibpkg}
MyProxy is open source software for managing X.509 Public Key Infrastructure 
(PKI) security credentials (certificates and private keys). MyProxy 
combines an online credential repository with an online certificate 
authority to allow users to securely obtain credentials when and where needed.
Users run myproxy-logon to authenticate and obtain credentials, including 
trusted CA certificates and Certificate Revocation Lists (CRLs). 

Package %{name}-libs contains runtime libs for MyProxy.

%package devel
Requires:      %{libpkg}%{?_isa}  = %{version}-%{release}
Requires:      globus-gss-assist-devel%{?_isa}  > 8
Requires:      globus-usage-devel%{?_isa} >= 3

Summary:       Develop X.509 Public Key Infrastructure (PKI) security credentials 
Group:         System Environment/Daemons

%description devel
MyProxy is open source software for managing X.509 Public Key Infrastructure 
(PKI) security credentials (certificates and private keys). MyProxy 
combines an online credential repository with an online certificate 
authority to allow users to securely obtain credentials when and where needed.
Users run myproxy-logon to authenticate and obtain credentials, including 
trusted CA certificates and Certificate Revocation Lists (CRLs). 

Package %{name}-devel contains development files for MyProxy.

%package server
%if 0%{?suse_version} == 0
Requires:         %{libpkg}%{?_isa} = %{version}-%{release}
Requires(pre):    shadow-utils
Requires(post):   chkconfig
Requires(preun):  chkconfig
Requires(preun):  initscripts
Requires(postun): initscripts
%else
Requires(pre):    shadow
Requires(preun):  sysconfig
Requires(preun):  aaa_base
Requires(postun): sysconfig
Requires(postun): aaa_base
%endif
Summary:          Server for X.509 Public Key Infrastructure (PKI) security credentials 
Group:            System Environment/Daemons

%description server
MyProxy is open source software for managing X.509 Public Key Infrastructure 
(PKI) security credentials (certificates and private keys). MyProxy 
combines an online credential repository with an online certificate 
authority to allow users to securely obtain credentials when and where needed.
Users run myproxy-logon to authenticate and obtain credentials, including 
trusted CA certificates and Certificate Revocation Lists (CRLs). 

Package %{name}-server contains the MyProxy server.

# Create a sepeate admin clients package since they
# not needed for normal operation and pull in
# a load of perl dependencies.
%package       admin
Requires:      %{libpkg}%{?_isa} = %{version}-%{release}
Requires:      myproxy-server = %{version}-%{release}
Requires:      myproxy = %{version}-%{release}
Requires:      globus-gsi-cert-utils-progs >= 8
Summary:       Server for X.509 Public Key Infrastructure (PKI) security credentials 
Group:         System Environment/Daemons

%description admin
MyProxy is open source software for managing X.509 Public Key Infrastructure 
(PKI) security credentials (certificates and private keys). MyProxy 
combines an online credential repository with an online certificate 
authority to allow users to securely obtain credentials when and where needed.
Users run myproxy-logon to authenticate and obtain credentials, including 
trusted CA certificates and Certificate Revocation Lists (CRLs). 

Package %{name}-admin contains the MyProxy server admin commands.

%package doc
Requires:      myproxy = %{version}-%{release}
Summary:       Documentation for X.509 Public Key Infrastructure (PKI) security credentials 
Group:         Documentation

%description doc
MyProxy is open source software for managing X.509 Public Key Infrastructure 
(PKI) security credentials (certificates and private keys). MyProxy 
combines an online credential repository with an online certificate 
authority to allow users to securely obtain credentials when and where needed.
Users run myproxy-logon to authenticate and obtain credentials, including 
trusted CA certificates and Certificate Revocation Lists (CRLs). 

Package %{name}-doc contains the MyProxy documentation.


%if %{?rhel}%{!?rhel:0} > 5 || %{?fedora}%{!?fedora:0} > 0
%package voms
Summary:       Manage X.509 Public Key Infrastructure (PKI) security credentials 
Group:         System Environment/Daemons
Obsoletes:     myproxy < 5.1-3
Requires:      voms-clients

%description voms
MyProxy is open source software for managing X.509 Public Key Infrastructure 
(PKI) security credentials (certificates and private keys). MyProxy 
combines an online credential repository with an online certificate 
authority to allow users to securely obtain credentials when and where needed.
Users run myproxy-logon to authenticate and obtain credentials, including 
trusted CA certificates and Certificate Revocation Lists (CRLs). 

Package %{name}-libs contains runtime libs for MyProxy to use VOMS.
%endif

%prep
%setup -q -n myproxy-%{version}

%build
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global initscript_config_path %{_localstatedir}/adm/fillup-templates/sysconfig.myproxy-server
%else
%global initscript_config_path %{_sysconfdir}/sysconfig/myproxy-server 
%endif

rm -f pkgdata/Makefile.am

%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7
rm -rf autom4te.cache
autoreconf -if
%endif

with_kerberos5=--with-kerberos5=%{_usr}
with_sasl2=--with-sasl2=%{_usr}

%if %{?fedora}%{!?fedora:0} > 0 || %{?rhel}%{!?rhel:0} > 5
%configure --with-openldap=%{_usr} \
           --with-voms=%{_usr} \
           --with-kerberos5=%{_usr} \
           --with-sasl2=%{_usr} \
           --includedir=%{_usr}/include/globus
%else
%configure --without-openldap \
           --without-voms \
           %{with_kerberos5} \
           %{with_sasl2} \
           --includedir=%{_usr}/include/globus
%endif
make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

find $RPM_BUILD_ROOT%{_libdir} -name 'lib*.la' -exec rm -v '{}' \;

# Remove static libraries (.a files)
find $RPM_BUILD_ROOT%{_libdir} -name 'lib*.a' -exec rm -v '{}' \;

# Remove the test script wrapper
rm -f $RPM_BUILD_ROOT%{_sbindir}/myproxy-test-wrapper

# No need for myproxy-server-setup since the rpm will perform
# the needed setup
rm $RPM_BUILD_ROOT%{_sbindir}/myproxy-server-setup


# We are going to zip the man pages later in the package so we need to
# correct the gpt data in anticipation.

mkdir -p  $RPM_BUILD_ROOT%{_defaultdocdir}/%{name}-doc-%{version}/extras

for FILE in login.html myproxy-accepted-credentials-mapapp myproxy-cert-checker myproxy-certificate-mapapp \
             myproxy-certreq-checker myproxy-crl.cron myproxy.cron myproxy-get-delegation.cgi \
             myproxy-get-trustroots.cron myproxy-passphrase-policy myproxy-revoke 
do
   mv $RPM_BUILD_ROOT%{_usr}/share/%{name}/$FILE \
      $RPM_BUILD_ROOT%{_defaultdocdir}/%{name}-doc-%{version}/extras/.
done

mkdir -p $RPM_BUILD_ROOT%{_defaultdocdir}/%{name}-%{version}
for FILE in INSTALL LICENSE LICENSE.* PROTOCOL README VERSION
do 
%if 0%{?suse_version} == 0
  mv  $RPM_BUILD_ROOT%{_usr}/share/%{name}/$FILE \
      $RPM_BUILD_ROOT%{_defaultdocdir}/%{name}-%{version}/.
%else
  mv  $RPM_BUILD_ROOT%{_usr}/share/%{name}/$FILE \
      $RPM_BUILD_ROOT%{_defaultdocdir}/%{name}-%{version}/.
%endif
done

# Remove irrelavent example configuration files.
for FILE in etc.inetd.conf.modifications etc.init.d.myproxy.nonroot etc.services.modifications  \
            etc.xinetd.myproxy etc.init.d.myproxy
do
  rm $RPM_BUILD_ROOT%{_usr}/share/%{name}/$FILE
done

# Generate pkg-config file from GPT metadata
# FIXME: This seems to already be generated by configure and
# globus-gpt2pkg-config is not (yet?) available GT5.2 Alpha globus-core. Investigate!
#mkdir -p $RPM_BUILD_ROOT%{_libdir}/pkgconfig
#%{_datadir}/globus/globus-gpt2pkg-config pkgdata/pkg_data_%{flavor}_dev.gpt > \
#  $RPM_BUILD_ROOT%{_libdir}/pkgconfig/%{name}.pc


# Move example configuration file into place.
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}
mv $RPM_BUILD_ROOT%{_datadir}/%{name}/myproxy-server.config \
   $RPM_BUILD_ROOT%{_sysconfdir}


mkdir -p $RPM_BUILD_ROOT%{_initddir}
%if %{?suse_version}%{!?suse_version:0} >= 1315
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/adm/fillup-templates
%else
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/sysconfig
%endif

install  -m 644 myproxy.sysconfig $RPM_BUILD_ROOT%{initscript_config_path}

%if 0%{?suse_version} == 0
install  -m 755 myproxy.init $RPM_BUILD_ROOT%{_initddir}/myproxy-server
%else
cat <<'EOF' > $RPM_BUILD_ROOT%{_initddir}/myproxy-server
#!/bin/sh
#
# myproxy-server - Server for X.509 Public Key Infrastructure (PKI) security credentials
#
# chkconfig: - 55 25
# description:  Server for X.509 Public Key Infrastructure (PKI) security credentials
#
### BEGIN INIT INFO
# Provides: myproxy-server
# Required-Start:  $remote_fs $network $syslog
# Required-Stop:  $remote_fs $syslog
# Should-Start:  $syslog
# Should-Stop:  $network $syslog
# Default-Stop: 0 1 6
# Default-Start: 2 3 5
# Short-Description: Startup the MyProxy server daemon
# Description: Server for X.509 Public Key Infrastructure (PKI) security credentials
### END INIT INFO

# Source function library.
. /lib/lsb/init-functions

exec="/usr/sbin/myproxy-server"
prog=$(basename $exec)

# Defaults
MYPROXY_USER=myproxy
MYPROXY_OPTIONS="-s /var/lib/myproxy"
X509_USER_CERT=/etc/grid-security/myproxy/hostcert.pem
X509_USER_KEY=/etc/grid-security/myproxy/hostkey.pem
export X509_USER_CERT
export X509_USER_KEY
PIDFILE=/var/run/myproxy.pid

# Override defaults here.
[ -e /etc/sysconfig/$prog ] && . /etc/sysconfig/$prog

# Start/Stop the myproxy daemon as user $MYPROXY_USER
# Is there a better LSB idiom for this?
if [ "$(id -u)" = 0 ]; then
    userexist="$(getent passwd "$MYPROXY_USER" | cut -d: -f3)"
    if [ "$userexist" != "" ] && [ "$userexist" != 0 ]; then
        exec su "$MYPROXY_USER" -s /bin/sh -c "$0 ${1+"$@"}"
    fi
fi

# A few sanity checks 
if [ "$1" != "status" ]; then
        [ ! -f $X509_USER_KEY ]  && log_failure_msg "$prog: No hostkey file"  && exit 0
        [ ! -r $X509_USER_KEY ]  && log_failure_msg "$prog: Unable to read hostkey file $X509_USER_KEY"  && exit 0
        [ ! -r $X509_USER_CERT ] && log_failure_msg "$prog: No hostcert file" && exit 0
        [ ! -r $X509_USER_CERT ] && log_failure_msg "$prog: Unable to read hostcert file" && exit 0
fi

start() {
    pidofproc $prog > /dev/null && log_warning_msg "$prog already running" && exit 0
    cd /
    X509_USER_CERT=$X509_USER_CERT X509_USER_KEY=$X509_USER_KEY start_daemon -p $PIDFILE "$exec" ${MYPROXY_OPTIONS}
    retval="$?"
    if [ "$retval" -eq 0 ]; then
        log_success_msg "Started $prog"
        pidofproc "$exec" > "$PIDFILE"
    else
        log_failure_msg "Error starting $prog"
    fi
    return $retval
}

stop() {
    killproc -p $PIDFILE "$exec"
    retval=$?
    if [ "$retval" -eq 0 ]; then
        log_success_msg "Stopped $prog"
    else
        log_success_msg "Error stopping $prog"
    fi
    return $retval
}

restart() {
    stop
    start
}

case "$1" in
    start|stop|restart)
        $1
        ;;
    force-reload)
        restart
        ;;
    status)
        pidofproc -p $PIDFILE $prog > /dev/null
        result="$?"
        if [ "$result" -eq 0 ]; then
            log_success_msg "$prog is running"
        else
            log_failure_msg "$prog is not running"
        fi
        exit $result
        ;;
    try-restart|condrestart)
        if pidofproc -p $PIDFILE $prog >/dev/null ; then
            restart
        fi
        ;;
    reload)
        # If config can be reloaded without restarting, implement it here,
        # remove the "exit", and add "reload" to the usage message below.
        # For example:
        pidofproc -p $PIDFILE $prog >/dev/null || exit 3
        killproc -p $PIDFILE $prog -HUP
        ;;
    *)
        echo $"Usage: $0 {start|stop|status|restart|reload|try-restart|force-reload}"
        exit 2
esac
EOF
chmod 755 $RPM_BUILD_ROOT%{_initddir}/myproxy-server
%endif
mkdir -p $RPM_BUILD_ROOT%{_var}/lib/myproxy

# Create a directory to hold myproxy owned host certificates.
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/grid-security/myproxy

%clean
rm -rf $RPM_BUILD_ROOT


%check 
PATH=.:$PATH make check

%post %{nlibpkg} -p /sbin/ldconfig
%postun %{nlibpkg} -p /sbin/ldconfig

%pre server
PATH=$PATH:/usr/sbin:/sbin
getent group myproxy >/dev/null || groupadd -r myproxy
getent passwd myproxy >/dev/null || \
useradd -r -g myproxy -d %{_var}/lib/myproxy -s /sbin/nologin \
   -c "User to run the MyProxy service" myproxy
exit 0

%post server
%if %{?suse_version}%{!?suse_version:0} >= 1315
%fillup_and_insserv -n myproxy-server myproxy-server
%else
/sbin/chkconfig --add myproxy-server
%endif

%preun server
%if %{?suse_version}%{!?suse_version:0} >= 1315
%stop_on_removal service
%else
if [ $1 = 0 ] ; then
    /sbin/service myproxy-server stop >/dev/null 2>&1
    /sbin/chkconfig --del myproxy-server
fi
%endif

%postun server
%if %{?suse_version}%{!?suse_version:0} >= 1315
%restart_on_update service
%insserv_cleanup
%else
if [ "$1" -eq "1" ] ; then
    /sbin/service myproxy-server condrestart >/dev/null 2>&1 || :
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

%{_mandir}/man1/myproxy-change-pass-phrase.1.gz
%{_mandir}/man1/myproxy-destroy.1.gz
%{_mandir}/man1/myproxy-get-delegation.1.gz
%{_mandir}/man1/myproxy-info.1.gz
%{_mandir}/man1/myproxy-init.1.gz
%{_mandir}/man1/myproxy-logon.1.gz
%{_mandir}/man1/myproxy-retrieve.1.gz
%{_mandir}/man1/myproxy-store.1.gz
%doc %{_defaultdocdir}/%{name}-%{version}

%files %{nlibpkg}
%defattr(-,root,root,-)
%{_libdir}/libmyproxy.so.*

%files server
%defattr(-,root,root,-)
%{_sbindir}/myproxy-server
%{_initddir}/myproxy-server
%config(noreplace)    %{_sysconfdir}/myproxy-server.config
%config(noreplace)    %{initscript_config_path}
# myproxy-server wants exactly 700 permission on its data 
# which is just fine.
%attr(0700,myproxy,myproxy) %dir %{_var}/lib/myproxy
%dir %{_sysconfdir}/grid-security/myproxy

%{_mandir}/man8/myproxy-server.8.gz
%{_mandir}/man5/myproxy-server.config.5.gz
%dir %{_datadir}/myproxy
%{_datadir}/myproxy/myproxy-server.conf
%{_datadir}/myproxy/myproxy-server.service

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
%{_mandir}/man8/myproxy-admin-addservice.8.gz
%{_mandir}/man8/myproxy-admin-adduser.8.gz
%{_mandir}/man8/myproxy-admin-change-pass.8.gz
%{_mandir}/man8/myproxy-admin-load-credential.8.gz
%{_mandir}/man8/myproxy-admin-query.8.gz
%{_mandir}/man8/myproxy-replicate.8.gz

%files doc
%defattr(-,root,root,-)
%doc %{_defaultdocdir}/%{name}-doc-%{version}

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/myproxy.h
%{_includedir}/globus/myproxy_authorization.h
%{_includedir}/globus/myproxy_constants.h
%{_includedir}/globus/myproxy_creds.h
%{_includedir}/globus/myproxy_delegation.h
%{_includedir}/globus/myproxy_log.h
%{_includedir}/globus/myproxy_protocol.h
%{_includedir}/globus/myproxy_read_pass.h
%{_includedir}/globus/myproxy_sasl_client.h
%{_includedir}/globus/myproxy_sasl_server.h
%{_includedir}/globus/myproxy_server.h
%{_includedir}/globus/verror.h
%{_libdir}/libmyproxy.so
%{_libdir}/pkgconfig/myproxy.pc

%if %{?rhel}%{!?rhel:0} > 5 || %{?fedora}%{!?fedora:0} > 0
%files voms
%defattr(-,root,root,-)
%{_libdir}/libmyproxy_voms.so
%endif

%changelog
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

