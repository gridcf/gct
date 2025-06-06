%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-gridftp-server
%global soname 6
%global _name %(echo %{name} | tr - _)
Version:	13.28
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Globus GridFTP Server

Group:		System Environment/Libraries
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	gcc
BuildRequires:	globus-common-devel >= 17
BuildRequires:	globus-xio-devel >= 5
BuildRequires:	globus-xio-gsi-driver-devel >= 2
BuildRequires:	globus-gfork-devel >= 3
BuildRequires:	globus-gridftp-server-control-devel >= 9
BuildRequires:	globus-ftp-control-devel >= 7
BuildRequires:	globus-authz-devel >= 2
BuildRequires:	globus-gssapi-gsi-devel >= 10
BuildRequires:	globus-gss-assist-devel >= 9
BuildRequires:	globus-gsi-credential-devel >= 6
BuildRequires:	globus-gsi-sysconfig-devel >= 5
BuildRequires:	globus-io-devel >= 9
%if %{?suse_version}%{!?suse_version:0}
BuildRequires:	libopenssl-devel
%else
BuildRequires:	openssl-devel
%endif
BuildRequires:	zlib-devel
%if ! %{?suse_version}%{!?suse_version:0}
BuildRequires:	perl-generators
%endif
#		Additional requirements for make check
BuildRequires:	openssl
%if %{?fedora}%{!?fedora:0} >= 21 || %{?rhel}%{!?rhel:0} >= 5
#		Used for some tests which are skipped if not present
BuildRequires:	fakeroot
%endif

%if %{?suse_version}%{!?suse_version:0}
%global mainpkg lib%{_name}%{soname}
%global nmainpkg -n %{mainpkg}
%else
%global mainpkg %{name}
%endif

%if %{?nmainpkg:1}%{!?nmainpkg:0}
%package %{?nmainpkg}
Summary:	Grid Community Toolkit - Globus GridFTP Server
Group:		System Environment/Libraries
Provides:	%{name} = %{version}-%{release}
Obsoletes:	%{name} < %{version}-%{release}
%endif

Requires:	globus-xio-gsi-driver%{?_isa} >= 2
Requires:	globus-xio-udt-driver%{?_isa} >= 1
Requires:	globus-common%{?_isa} >= 17
Requires:	globus-xio%{?_isa} >= 5
Requires:	globus-gridftp-server-control%{?_isa} >= 9
Requires:	globus-ftp-control%{?_isa} >= 7

%package progs
Summary:	Grid Community Toolkit - Globus GridFTP Server Programs
Group:		Applications/Internet
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}
%if %{?suse_version}%{!?suse_version:0}
Requires(post):		aaa-base
Requires(preun):	aaa-base
Requires(postun):	aaa-base
%else
Requires(post):		chkconfig
Requires(preun):	chkconfig
Requires(preun):	initscripts
Requires(postun):	initscripts
%endif

%package devel
Summary:	Grid Community Toolkit - Globus GridFTP Server Development Files
Group:		Development/Libraries
Requires:	%{mainpkg}%{?_isa} = %{version}-%{release}

%if %{?nmainpkg:1}%{!?nmainpkg:0}
%description %{?nmainpkg}
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{mainpkg} package contains:
Globus GridFTP Server
%endif

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Globus GridFTP Server

%description progs
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-progs package contains:
Globus GridFTP Server Programs

%description devel
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name}-devel package contains:
Globus GridFTP Server Development Files

%prep
%setup -q -n %{_name}-%{version}

%build
export GRIDMAP=%{_sysconfdir}/grid-security/grid-mapfile
export GLOBUS_VERSION=6.2
%configure --disable-static \
	   --includedir=%{_includedir}/globus \
%if %{?suse_version}%{!?suse_version:0}
	   --with-default-runlevels=235 \
%endif
	   --libexecdir=%{_datadir}/globus \
	   --docdir=%{_pkgdocdir}

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT

# Remove libtool archives (.la files)
rm $RPM_BUILD_ROOT%{_libdir}/*.la

if [ "%{_initddir}" != "%{_sysconfdir}/init.d" ] ; then
    mkdir -p $RPM_BUILD_ROOT%{_initddir}
    mv $RPM_BUILD_ROOT%{_sysconfdir}/init.d/* $RPM_BUILD_ROOT%{_initddir}
    rmdir $RPM_BUILD_ROOT%{_sysconfdir}/init.d
fi

mv $RPM_BUILD_ROOT%{_sysconfdir}/gridftp.conf.default \
   $RPM_BUILD_ROOT%{_sysconfdir}/gridftp.conf
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/xinetd.d
mv $RPM_BUILD_ROOT%{_sysconfdir}/gridftp.xinetd.default \
   $RPM_BUILD_ROOT%{_sysconfdir}/xinetd.d/gridftp
mv $RPM_BUILD_ROOT%{_sysconfdir}/gridftp.gfork.default \
   $RPM_BUILD_ROOT%{_sysconfdir}/gridftp.gfork

# No need for environment in conf files
sed '/ env /d' -i $RPM_BUILD_ROOT%{_sysconfdir}/gridftp.gfork
sed '/^env /d' -i $RPM_BUILD_ROOT%{_sysconfdir}/xinetd.d/gridftp

%check
make %{?_smp_mflags} check VERBOSE=1

%post %{?nmainpkg} -p /sbin/ldconfig

%postun %{?nmainpkg} -p /sbin/ldconfig

%post progs
if [ $1 -eq 1 ]; then
    /sbin/chkconfig --add %{name}
    /sbin/chkconfig --add globus-gridftp-sshftp
fi

%preun progs
if [ $1 -eq 0 ]; then
    /sbin/service %{name} stop > /dev/null 2>&1 || :
    /sbin/service globus-gridftp-sshftp stop > /dev/null 2>&1 || :
    /sbin/chkconfig --del %{name}
    /sbin/chkconfig --del globus-gridftp-sshftp
fi

%postun progs
if [ $1 -ge 1 ]; then
    /sbin/service %{name} condrestart > /dev/null 2>&1 || :
    /sbin/service globus-gridftp-sshftp condrestart > /dev/null 2>&1 || :
fi

%files %{?nmainpkg}
%defattr(-,root,root,-)
%{_libdir}/libglobus_gridftp_server.so.*
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%files progs
%defattr(-,root,root,-)
%{_sbindir}/gfs-dynbe-client
%{_sbindir}/gfs-gfork-master
%{_sbindir}/globus-gridftp-password
%{_sbindir}/globus-gridftp-server
%{_sbindir}/globus-gridftp-server-enable-sshftp
%{_sbindir}/globus-gridftp-server-setup-chroot
%config(noreplace) %{_sysconfdir}/gridftp.conf
%config(noreplace) %{_sysconfdir}/gridftp.gfork
%config(noreplace) %{_sysconfdir}/xinetd.d/gridftp
%{_initddir}/%{name}
%{_initddir}/globus-gridftp-sshftp
%doc %{_mandir}/man8/globus-gridftp-password.8*
%doc %{_mandir}/man8/globus-gridftp-server.8*
%doc %{_mandir}/man8/globus-gridftp-server-setup-chroot.8*

%files devel
%defattr(-,root,root,-)
%{_includedir}/globus/*
%{_libdir}/libglobus_gridftp_server.so
%{_libdir}/pkgconfig/%{name}.pc

%changelog
* Fri Jan 17 2025 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.28-1
- Fix incompatible pointer errors (gcc 15)

* Sat Mar 16 2024 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.27-1
- Handle 64 bit time_t on 32 bit systems

* Fri Mar 08 2024 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.26-1
- Correct spelling error found by lintian

* Mon Nov 28 2022 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.25-1
- Fix buffer overflow in test

* Thu Mar 10 2022 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.24-1
- Fix some compiler warnings
- Fix unbalanced mutex lock/unlock

* Sun Mar 06 2022 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.23-1
- Use sha256 hash when generating test certificates

* Fri Aug 20 2021 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.22-1
- Typo fixes

* Thu Jan 09 2020 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.21-1
- Restore log transfer functionality that was accidentally removed

* Thu Aug 08 2019 Frank Scheiner <scheiner@hlrs.de> - 13.20-1
- Fix problems between dual-stack (IPv4/IPv6) servers and IPv4-only clients

* Mon Jul 22 2019 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.19-1
- Avoid unknown secondary groups in test. Causes failures on launchpad

* Fri Jun 07 2019 Globus Toolkit <support@globus.org> - 13.18-1
- Add support for supported checksum advertising
- Add support for SHA1, SHA256, SHA512 to POSIX DSI

* Mon Jun 03 2019 Globus Toolkit <support@globus.org> - 13.17-1
- add simple checksum read throttling

* Wed Apr 17 2019 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.16-1
- Remove obsolete acconfig.h file

* Sun Apr 14 2019 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.15-1
- Fix doxygen warning

* Thu Apr 11 2019 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.14-1
- Update documentation links to always point to the latest documentation

* Mon Apr 01 2019 Globus Toolkit <support@globus.org> - 13.13-1
- send markers in stream mode when requested by 'OPTS RETR Markers=n;'

* Fri Mar 01 2019 Globus Toolkit <support@globus.org> - 13.12-1
- fake stat responses when slow listings enabled
- win: error on un-stat()-able files in directory listing

* Thu Feb 07 2019 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.11-1
- Remove usage statistics collection support

* Wed Feb 06 2019 Globus Toolkit <support@globus.org> - 13.10-1
- improvements for filesystems that encounter listing timeouts

* Mon Nov 05 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.9-2
- Bump GCT release version to 6.2

* Mon Sep 10 2018 Globus Toolkit <support@globus.org> - 13.9-1
- Fix data_node restrict path

* Tue Aug 28 2018 Globus Toolkit <support@globus.org> - 13.8-1
- log remote http connection address for legacy s3 transfers

* Tue Aug 07 2018 Globus Toolkit <support@globus.org> - 13.7-1
- fix initscript non-lsb status return codes

* Mon Jul 16 2018 Globus Toolkit <support@globus.org> - 13.6-1
- fix daemon config parsing not catching env vars

* Fri Jul 13 2018 Globus Toolkit <support@globus.org> - 13.5-1
- force ipc encryption if server configuration requires
- fix old ipc bug making it hard to diagnose racy connection failures

* Fri Jun 15 2018 Globus Toolkit <support@globus.org> - 13.4-1
- win: fix path restrictions on /

* Sat May 05 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.3-1
- Use 2048 bit RSA key for tests

* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.2-1
- First Grid Community Toolkit release
- Remove support for openssl101e (RHEL5 is EOL)
- Disable usage statistics reporting by default
- Add man page for globus-gridftp-password - contribution from IGE

* Wed Feb 07 2018 Globus Toolkit <support@globus.org> - 13.1-1
- win32 fix

* Sat Jan 20 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 13.0-1
- Add option to send IPv6 address in EPSV response
- Add function to get the command string
- Be more selective in what config files we skip
- Add unames for GNU/Hurd and kfreebsd to chroot setup script

* Wed Nov 08 2017 Globus Toolkit <support@globus.org> - 12.4-1
- Improve search for user env in enable-sshftp script

* Mon Sep 25 2017 Globus Toolkit <support@globus.org> - 12.3-1
- preloaded module typo fix

* Tue Jun 20 2017 Globus Toolkit <support@globus.org> - 12.2-1
- Fix tests when getgroups() does not return effective gid

* Tue Apr 18 2017 Globus Toolkit <support@globus.org> - 12.1-1
- better delay for end of session ref check

* Mon Apr 10 2017 Globus Toolkit <support@globus.org> - 12.0-1
- Fix MDTM/UTIME on windows
- New error message format
- Configuration database

* Fri Oct 28 2016 Globus Toolkit <support@globus.org> - 11.8-1
- better MFMT fix for windows directories.  prior fix resulted in MDTM not matching MFMT depending on DST.

* Wed Oct 05 2016 Globus Toolkit <support@globus.org> - 11.7-1
- fix error response for MDTM/UTIME on windows

* Tue Oct 04 2016 Globus Toolkit <support@globus.org> - 11.6-1
- add zlib autoconf checks and only link in file module

* Tue Oct 04 2016 Globus Toolkit <support@globus.org> - 11.5-1
- add adler32 checksum support
- disable threads on the daemon process
- fix windows directory MDTM support

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 11.4-1
- Update for el.5 openssl101e, replace docbook with asciidoc

* Fri Aug 26 2016 Globus Toolkit <support@globus.org> - 11.3-6
- Updates for SLES 12

* Thu Aug 18 2016 Globus Toolkit <support@globus.org> - 11.3-1
- Makefile fix

* Tue Aug 16 2016 Globus Toolkit <support@globus.org> - 11.2-1
- Updates for OpenSSL 1.1.0

* Fri Jul 15 2016 Globus Toolkit <support@globus.org> - 11.1-1
- fix forced ordering issues

* Thu Jul 14 2016 Globus Toolkit <support@globus.org> - 11.0-2
- bump aging version

* Thu Jul 14 2016 Globus Toolkit <support@globus.org> - 11.0-1
- add forced data ordering on reads to DSI interface

* Wed Jun 29 2016 Globus Toolkit <support@globus.org> - 10.6-1
- add Globus task id to transfer log

* Mon Jun 27 2016 Globus Toolkit <support@globus.org> - 10.5-1
- Don't errantly kill a transfer due to timeout while client is still connected

* Thu May 19 2016 Globus Toolkit <support@globus.org> - 10.4-2
- fix broken remote_node auth without sharing
- Add openssl build dependency

* Wed May 18 2016 Globus Toolkit <support@globus.org> - 10.3-1
- fix configuration for ipc_interface
- fix remote_node connection failing when ipc_subject isn't used

* Tue May 03 2016 Globus Toolkit <support@globus.org> - 10.2-1
- Spelling

* Tue May 03 2016 Globus Toolkit <support@globus.org> - 10.1-1
- Don't overwite LDFLAGS

* Mon May 02 2016 Globus Toolkit <support@globus.org> - 10.0-1
- Updates for https server support

* Thu Apr 21 2016 Globus Toolkit <support@globus.org> - 9.9-1
- add -dlpreload force tests

* Mon Apr 18 2016 Globus Toolkit <support@globus.org> - 9.8-1
- Use prelinks for tests so that they run on El Capitan

* Thu Apr 14 2016 Globus Toolkit <support@globus.org> - 9.7-1
- fix crash when storattr is used without modify

* Thu Mar 24 2016 Globus Toolkit <support@globus.org> - 9.6-1
- add SITE WHOAMI command to return currently authenticated user

* Tue Mar 15 2016 Globus Toolkit <support@globus.org> - 9.5-1
- update manpage for -encrypt-data

* Wed Dec 16 2015 Globus Toolkit <support@globus.org> - 9.4-1
- fix mem error when sharing

* Mon Nov 23 2015 Globus Toolkit <support@globus.org> - 9.3-1
- Add configuration to require encrypted data channels

* Fri Nov 20 2015 Globus Toolkit <support@globus.org> - 9.2-1
- More robust cmp function

* Tue Nov 03 2015 Globus Toolkit <support@globus.org> - 9.1-1
- fix for thread race crash between sequential transfers
- fix for partial stat punting when passed a single entry
- fix for double free on transfer failure race

* Fri Oct 23 2015 Globus Toolkit <support@globus.org> - 9.0-1
- add SITE STORATTR command and associated DSI api

* Mon Sep 28 2015 Globus Toolkit <support@globus.org> - 8.9-1
- home dir is always / when shared chroot

* Mon Sep 28 2015 Globus Toolkit <support@globus.org> - 8.8-1
- Update internal home dir when DSI supplies one

* Fri Aug 21 2015 Globus Toolkit <support@globus.org> - 8.7-2
- Add fakeroot dependency for tests on platforms that support it

* Fri Aug 21 2015 Globus Toolkit <support@globus.org> - 8.7-1
- Portability fixes for globus-gridftp-server-setup-chroot

* Fri Aug 21 2015 Globus Toolkit <support@globus.org> - 8.6-1
- Improve globus-gridftp-server-setup-chroot
- Add manpage for globus-gridftp-server-setup-chroot
- Add tests for globus-gridftp-server-setup-chroot

* Mon Aug 10 2015 Globus Toolkit <support@globus.org> - 8.5-1
- Fix libtool test run problem

* Sat Aug 08 2015 Globus Toolkit <support@globus.org> - 8.4-1
- Test fixes

* Fri Aug 07 2015 Globus Toolkit <support@globus.org> - 8.3-1
- Fix preload_link checking

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 8.2-1
- Allow test cases to run in installer build
- Improve test coverage

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 8.1-3
- Add vendor

* Wed Aug 05 2015 Globus Toolkit <support@globus.org> - 8.1-2
- GT-622: GridFTP server crash with sharing group permissions
- Add make check to rpm build

* Thu Jul 23 2015 Globus Toolkit <support@globus.org> - 8.0-1
- GT-517: add update_bytes* api that sets byte counters and range markers seperately

* Fri Jun 05 2015 Globus Toolkit <support@globus.org> - 7.26-1
- Fix GLOBUS_VERSION detection during configure from installer

* Tue Apr 07 2015 Globus Toolkit <support@globus.org> - 7.25-1
- Fix order of drivers when using netmgr

* Fri Mar 27 2015 Globus Toolkit <support@globus.org> - 7.24-1
- fix netmanager crash
- allow netmanager calls when taskid isn't set

* Mon Mar 16 2015 Globus Toolkit <support@globus.org> - 7.23-1
- fix threads commandline arg processing
- prevent parse error on pre-init envs from raising assertion

* Fri Mar 06 2015 Globus Toolkit <support@globus.org> - 7.22-1
- windows fix

* Fri Mar 06 2015 Globus Toolkit <support@globus.org> - 7.21-1
- GT-586: Restrict sharing based on username or group membership
- GT-552: don't enable udt without threads
- GT-585: Environrment and threading config not loaded from config dir
- Ignore config.d files with a '.' in name
- always install udt driver

* Tue Jan 06 2015 Globus Toolkit <support@globus.org> - 7.20-1
- Fix autoreconf error on some setups

* Tue Dec 23 2014 Globus Toolkit <support@globus.org> - 7.19-1
- Fix -help long line formatting

* Mon Dec 22 2014 Globus Toolkit <support@globus.org> - 7.18-1
- GT-575: Add support for the network manager driver.

* Fri Dec 05 2014 Globus Toolkit <support@globus.org> - 7.17-1
- Fix share file creation errors on bad fuse filesystems.

* Sun Nov 16 2014 Globus Toolkit <support@globus.org> - 7.16-1
- don't attempt to get retransmit count on http transfer

* Mon Nov 10 2014 Globus Toolkit <support@globus.org> - 7.15-1
- Remove reference to Globus::Core::Paths

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 7.14-1
- Fix logging of IPv6 address

* Tue Oct 28 2014 Globus Toolkit <support@globus.org> - 7.13-1
- GT-477: Tracking TCP retransmits on the GridFTP server

* Tue Sep 23 2014 Globus Toolkit <support@globus.org> - 7.12-1
- Add missing dependencies
- Quiet some autoconf/automake warnings
- Fix some typos in help messages

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 7.11-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 7.10-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 7.10-1
- Merge changes from Mattias Ellert

* Tue May 27 2014 Globus Toolkit <support@globus.org> - 7.9-1
- Use globus_libc_unsetenv

* Tue May 27 2014 Globus Toolkit <support@globus.org> - 7.8-1
- Use package-named config.h

* Thu Apr 24 2014 Globus Toolkit <support@globus.org> - 7.7-1
- Packaging fixes

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 7.6-1
- Version bump for consistency

* Thu Feb 27 2014 Globus Toolkit <support@globus.org> - 7.4-1
- Packaging fixes, Warning Cleanup

* Thu Feb 20 2014 Globus Toolkit <support@globus.org> - 7.3-1
- Crash on DCAU N with custom net stack

* Fri Feb 14 2014 Globus Toolkit <support@globus.org> - 7.2-1
- Packaging fixes

* Thu Feb 13 2014 Globus Toolkit <support@globus.org> - 7.1-1
- Packagin fixes

* Tue Jan 21 2014 Globus Toolkit <support@globus.org> - 7.0-1
- Repackage for GT6 without GPT

* Mon Oct 28 2013 Globus Toolkit <support@globus.org> - 6.38-1
- Update dependencies for new credential/assist functions

* Mon Oct 28 2013 Globus Toolkit <support@globus.org> - 6.37-2
- Update dependencies for new credential/assist functions

* Tue Oct 15 2013 Globus Toolkit <support@globus.org> - 6.37-1
- GT-374: Can't share files in a path structure with symlinks
- GT-428: Improve handling of hanging GridFTP server processes
- GT-469: MFMT/UTIME update access time but shouldn't

* Thu Aug 15 2013 Globus Toolkit <support@globus.org> - 6.36-1
- GT-368: Fix log message concatination when writing to syslog
- GT-420: revert to documented behavior for restricted paths

* Wed Jul 31 2013 Globus Toolkit <support@globus.org> - 6.35-1
- GT-428: improve handling of hanging server processes

* Wed Jul 31 2013 Globus Toolkit <support@globus.org> - 6.34-1
- GT-428: improve handling of hanging server processes

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 6.33-2
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Wed Jun 19 2013 Globus Toolkit <support@globus.org> - 6.33-1
- Add GLOBUS_OPENSSL in configure

* Wed Jun 05 2013 Globus Toolkit <support@globus.org> - 6.32-1
- GT-396: fix mlst on filenames that end in a newline
- GT-412: add -version-tag to set an identifier in the server version string
- fix minor memory leaks
- fix mlsx symlink target not urlencoding properly

* Tue Jun 04 2013 Globus Toolkit <support@globus.org> - 6.31-1
- GT-407 regression

* Mon Jun 03 2013 Globus Toolkit <support@globus.org> - 6.30-1
- GT-408: service globus-gridftp-server status returns incorrect status on SL5

* Sat Jun 01 2013 Globus Toolkit <support@globus.org> - 6.29-1
- GT-337: add UDT NAT traversal protocol
- GT-400: send confid when configured with default target

* Fri May 31 2013 Globus Toolkit <support@globus.org> - 6.28-1
- GT-407: globus-gridftp-server status returns 0 when not running on ubuntu

* Thu May 16 2013 Globus Toolkit <support@globus.org> - 6.27-1
- Allow variables in -sharing-rp
- fix 32/64 rpm conflicts
- create -sharing-state-dir when default
- correctly handle a share root containing "

* Wed May 08 2013 Globus Toolkit <support@globus.org> - 6.26-1
- GT-388: perform sharing access check inside the chroot
- fix chroot setup script for different MAKEDEV location

* Fri Apr 26 2013 Globus Toolkit <support@globus.org> - 6.25-1
- GT-365 control sharing by individual share ids
- GT-365 always restrict state dir when sharing

* Mon Apr 15 2013 Globus Toolkit <support@globus.org> - 6.24-1
- GT-365 verify sharing cert chain
- GT-365 update sharing config from sharing-file to state-dir
- GT-364 SSHFTP fixes

* Tue Mar 19 2013 Globus Toolkit <support@globus.org> - 6.23-1
- Update sharing to support a full cert chain at logon

* Mon Mar 18 2013 Globus Toolkit <support@globus.org> - 6.22-1
- GT-354: Compatibility with automake 1.13

* Wed Mar 06 2013 Globus Toolkit <support@globus.org> - 6.21-1
- missing build dependency

* Wed Mar 06 2013 Globus Toolkit <support@globus.org> - 6.20-1
- GT-365: Switch sharing user identification from DN to CERT

* Mon Feb 04 2013 Globus Toolkit <support@globus.org> - 6.19-1
- GT-302: Add initial sharing support to the GridFTP server
- GT-335: Update doc to clarify restrict_paths backend usage.
- GT-348: fix for logging of username after a hybrid mode striped transfer
- GT-351: fix for errors when surpassing config line limit, remove limits
- GT-353: avoid accessing new struct member if DSI isn't compatible
- GT-356: Add configuration and a command to make the sharing authorization file easier to manage
- GT-358: Invalid values for boolean config options silently sets the option false.

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 6.16-2
- 5.2.3

* Thu Nov 08 2012 Globus Toolkit <support@globus.org> - 6.16-1
- GT-299: fix race condition occuring when transfer finishes while COMMIT event is outstanding
- GT-304: fix bashism in sh script
- GT-310: clarify -rp-follow-symlinks help
- GT-314: fix crash when attempting striping in hybrid mode and backends are not available
- GT-316: log ip address of incoming connection after failure discovering hostname

* Wed Sep 19 2012 Michael Link <mlink@mcs.anl.gov> - 6.15-1
- GT-269: GridFTP servers do not report the DEST IP address in transfer logs or usage stats when configured for striping or split processes

* Tue Jul 17 2012 Joseph Bester <bester@mcs.anl.gov> - 6.14-1
- GT-254: Gridftp server uses dynamic string as sprintf argument

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 6.13-2
- GT 5.2.2 final

* Thu Jul 12 2012 Joseph Bester <bester@mcs.anl.gov> - 6.13-1
- GT-172: Removed custom MLSx tag feature
- GT-244: Cleaned up memory leaks
- GT-243: Fix needless frontend->backend connections

* Wed Jun 27 2012 Joseph Bester <bester@mcs.anl.gov> - 6.12-1
- GRIDFTP-164: improve dir streaming stability
- GRIDFTP-165: correct chunking of MLSC response
- GRIDFTP-165: fix MLSC over split processes
- GRIDFTP-196: fix behaviour for syntax errors in server options
- GRIDFTP-201: Add heartbeat/status markers to CKSM and RETR
- GRIDFTP-209: Add manpage for globus-gridftp-server
- GRIDFTP-212: GridFTP server doesn't build if PATH_MAX is not defined
- GRIDFTP-215: add MFMT synonym to SITE UTIME
- GRIDFTP-217: fix -connections-disabled for inetd
- GRIDFTP-218: add -fork-fallback
- GRIDFTP-219: allow prot without gsi
- GRIDFTP-221: additional changes towards maintaining backwards compatibility.
- GRIDFTP-221: backwards compatibility fix and future binary compatibility stability additions
- GRIDFTP-221: improvements to backwards compatibility
- GRIDFTP-222: fix threaded issues with streaming dir info for mlsd and mlsc
- GRIDFTP-224: Add option to set custom client starting/home directory
- GRIDFTP-224: make -home-dir option work correctly with -restrict-paths, clarify
- GRIDFTP-226: Fix recursed dir listings in split/striped server mode, when recursion wasn't requested.
- GRIDFTP-227: Add server option to enable threaded operation and set number of threads.
- GRIDFTP-228: Don't require delegated cred on initial log in.
- GRIDFTP-230: downgrade gfork not loaded message
- GT-152: GRIDFTP acts as wrong user when gridmap user doesnt exist
- GT-152: fix issues with CHMOD when mode_t is 2 bytes
- GT-164: add a hybrid mode to stripe configuration which only creates backend connections if client requests stripes.
- GT-167: ensure log files are created with acceptable default permissions
- GT-173: Allow a frontend->backend connection via admin defined credentials
- GT-3: gridftp server incorrectly handles relative path configuration values
- RIC-226: Some dependencies are missing in GPT metadata
- RIC-229: Clean up GPT metadata
- RIC-258: Can't rely on MKDIR_P

* Thu May 17 2012 Joseph Bester <bester@mcs.anl.gov> - 6.11-1
- GT-195: GridFTP acts as wrong user when user doesn't exist

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 6.10-2
- RHEL 4 patches

* Fri Apr 13 2012 Joseph Bester <bester@mcs.anl.gov> - 6.10-1
- RIC-258: Can't rely on MKDIR_P

* Tue Mar 27 2012  <mlink@mcs.anl.gov> - 6.9-1
- GRIDFTP-228: Don't require delegated cred on initial log in.

* Fri Mar 23 2012 Joseph Bester <bester@mcs.anl.gov> - 6.8-1
- GRIDFTP-227: Add server option to enable threaded operation and set number of threads.
- GRIDFTP-226: Fix recursed dir listings in split/striped server mode, when recursion wasn't requested.
- GRIDFTP-224: Add option to set custom client starting/home directory
- GRIDFTP-221: additional changes towards maintaining backwards compatibility.
- GRIDFTP-215: add MFMT synonym to SITE UTIME

* Tue Mar 06 2012 Joseph Bester <bester@mcs.anl.gov> - 6.7-1
- GRIDFTP-164: improve dir streaming stability
- GRIDFTP-165: correct chunking of MLSC response
- GRIDFTP-165: fix MLSC over split processes
- GRIDFTP-196: fix behaviour for syntax errors in server options
- GRIDFTP-201: Add heartbeat/status markers to CKSM and RETR
- GRIDFTP-209: Add manpage for globus-gridftp-server
- GRIDFTP-212: GridFTP server doesn't build if PATH_MAX is not defined
- GRIDFTP-217: fix -connections-disabled for inetd
- GRIDFTP-218: add -fork-fallback
- GRIDFTP-219: allow prot without gsi
- GRIDFTP-221: backwards compatibility fix and future binary compatibility
	       stability additions
- GRIDFTP-222: fix threaded issues with streaming dir info for mlsd and mlsc
- RIC-226: Some dependencies are missing in GPT metadata
- RIC-229: Clean up GPT metadata

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 6.6-1
- GRIDFTP-209: Add manpage for globus-gridftp-server
- GRIDFTP-212: GridFTP server doesn't build if PATH_MAX is not defined
- RIC-226: Some dependencies are missing in GPT metadata
- RIC-229: Clean up GPT metadata

* Mon Dec 12 2011 Joseph Bester <bester@mcs.anl.gov> - 6.5-1
- init script fixes

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 6.4-3
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 6.4-2
- Last sync prior to 5.2.0

* Fri Nov 11 2011 Joseph Bester <bester@mcs.anl.gov> - 6.3-1
- GRIDFTP-190: add in config dir loading

* Mon Oct 24 2011 Joseph Bester <bester@mcs.anl.gov> - 6.2-2
- Add explicit dependencies on >= 5.2 libraries
- Add backward-compatibility aging
- Fix %%post* scripts to check for -eq 1

* Fri Sep 23 2011 Joseph Bester <bester@mcs.anl.gov> - 6.1-1
- GRIDFTP-184: Detect and workaround bug in start_daemon for LSB < 4

* Wed Aug 31 2011 Joseph Bester <bester@mcs.anl.gov> - 6.0-3
- Add more config files for xinetd or gfork startup
- Update to Globus Toolkit 5.1.2

* Sat Jul 17 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 3.23-1
- Update to Globus Toolkit 5.0.2

* Wed Apr 14 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 3.21-1
- Update to Globus Toolkit 5.0.1

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 3.19-1
- Update to Globus Toolkit 5.0.0

* Mon Oct 19 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 3.17-2
- Fix location of default config file

* Thu Jul 30 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 3.17-1
- Autogenerated
