Name:           gridftp-hdfs
%global _name %(tr - _ <<< %{name})
Version:        2.0
Release:        1
Summary:        HDFS DSI plugin for GridFTP

Group:          System Environment/Daemons
License:        %{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:            http://twiki.grid.iu.edu/bin/view/Storage/HadoopInstallation
Source:         %{_name}-%{version}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

%if 0%{?suse_version} > 0
BuildRequires: java-sdk >= 1.7.0
%else
BuildRequires: java-devel
%endif

# Natively available in Fedora >= 20
%if %{?fedora}%{!?fedora:0} >= 20
BuildRequires: hadoop-devel
BuildRequires: libhdfs
%else
# Requires cloudera versions for other releases
BuildRequires: hadoop-libhdfs-devel
Requires: hadoop-libhdfs
%endif
BuildRequires:  openssl-devel
BuildRequires: globus-gridftp-server-devel >= 11
BuildRequires: globus-common-devel
BuildRequires: pkgconfig

Requires: globus-gridftp-server-progs >= 11
Requires: xinetd

Requires(preun): initscripts
Requires(preun): chkconfig
Requires(post): chkconfig
Requires(postun): initscripts
Requires(postun): xinetd

%description
HDFS DSI plugin for GridFTP

%prep

%setup -q -n %{_name}-%{version}

%build

%configure --with-java=/etc/alternatives/java_sdk --disable-static CPPFLAGS=-I/usr/include/hadoop LDFLAGS=-ldl

make %{?_smp_mflags}

%install

make DESTDIR=$RPM_BUILD_ROOT install

# Remove libtool .la files
rm -f $RPM_BUILD_ROOT%{_libdir}/*.la

# Remove the init script - in GT5.2, this gets bootstrapped appropriately
rm $RPM_BUILD_ROOT%{_sysconfdir}/sysconfig/gridftp.conf.d/%{name}-environment-bootstrap

%post
/sbin/ldconfig

/sbin/chkconfig --add %{name}

%preun
if [ "$1" = "0" ] ; then
    /sbin/service xinetd condrestart >/dev/null 2>&1
    /sbin/service %{name} stop >/dev/null 2>&1 || :
    /sbin/chkconfig --del %{name}
fi

%postun
/sbin/ldconfig
if [ "$1" -ge "1" ]; then
    /sbin/service xinetd condrestart >/dev/null 2>&1
    /sbin/service gridftp-hdfs condrestart >/dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%{_sbindir}/gridftp-hdfs-inetd
%{_bindir}/gridftp-hdfs-standalone
%{_libdir}/libglobus_gridftp_server_hdfs.so
%{_datadir}/%{name}/%{name}-environment
%config(noreplace) %{_sysconfdir}/xinetd.d/%{name}
%config(noreplace) %{_sysconfdir}/%{name}/gridftp-debug.conf
%config(noreplace) %{_sysconfdir}/%{name}/gridftp-inetd.conf
%config(noreplace) %{_sysconfdir}/%{name}/gridftp.conf
%config(noreplace) %{_sysconfdir}/%{name}/replica-map.conf
%config(noreplace) %{_sysconfdir}/sysconfig/gridftp.conf.d/%{name}
%{_sysconfdir}/init.d/%{name}

%changelog
* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 2.0-1
- First Grid Community Toolkit release
- Remove support for openssl101e (RHEL5 is EOL)

* Mon Sep 12 2016 Globus Toolkit <support@globus.org> - 1.5-1
- Updates for el.5 with openssl101e

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 1.4-2
- Rebuild after changes for el.5 with openssl101e

* Mon Jul 25 2016 Globus Toolkit <support@globus.org> - 1.4-1
- enable data ordering

* Fri Mar 11 2016 Globus Toolkit <support@globus.org> - 1.3-3
- add -ldl

* Fri Mar 11 2016 Globus Toolkit <support@globus.org> - 1.3-2
- Fix typo

* Fri Mar 11 2016 Globus Toolkit <support@globus.org> - 1.3-1
- Update jdk dependency for sles

* Mon Aug 10 2015 Globus Toolkit <support@globus.org> - 1.2-3
- Add pkgconfig build dependency

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 1.2-2
- Add vendor

* Tue May 19 2015 Globus Toolkit <support@globus.org> - 1.2-1
- Remove non-standard inline usage

* Mon Apr 20 2015 Globus Toolkit <support@globus.org> - 1.1-2
- Update to cloudera cdh5 dependency

* Fri Jan 09 2015 Globus Toolkit <support@globus.org> - 1.1-1
- Missing -module

* Wed Dec 10 2014 Joseph Bester <bester@mcs.anl.gov> - 1.0-2
- Source tarball name fix

* Fri Nov 14 2014 Joseph Bester <bester@mcs.anl.gov> - 1.0-1
- Imported into GT6
- Initial support for native hdfs on Fedora

* Tue Dec 06 2011 Brian Bockelman <bbockelm@cse.unl.edu> - 0.5.3-1
- Initial support for GlobusOnline.

* Sat Nov 19 2011 Brian Bockelman <bbockelm@cse.unl.edu> - 0.5.2-1
- Implement checksum support for gridftp-hdfs.

* Sat Oct 15 2011 Brian Bockelman <bbockelm@cse.unl.edu> - 0.5.1-1
Migrate as much of the RPM as possible to the OSG-style of gridftp initialization while maintaining GT 52 compatibility.

* Sat Sep 24 2011 Brian Bockelman <bbockelm@cse.unl.edu> - 0.5.0-1
- Redo gridftp to allow either xinetd or init startup; link with GT 5.2.

* Mon Jun 6 2011 Brian Bockelman <bbockelm@cse.unl.edu> 0.4.0-2
- Decrease logging verbosity.
- Make sure we always finish up the transfer.

* Mon Jun 6 2011 Brian Bockelman <bbockelm@cse.unl.edu> 0.4.0-1
- Rewrite of send/recv to clean up logic and error handling.
- Should improve multithreaded performance.

* Thu Jun 2 2011 Brian Bockelman <bbockelm@cse.unl.edu> 0.3.2-2
- Remove dependency on gratia probe.

* Thu May 26 2011 Brian Bockelman <bbockelm@cse.unl.edu> 0.3.2-1
- Attempt to fix the race issue for closing files.

* Tue Apr 26 2011 Brian Bockelman <bbockelm@cse.unl.edu> 0.3.1-1
- Fix listing of empty directories; error is now higher in the globus stack
- Fix listed modes in stat.
- Make everything friendly to dumping cores.
- On segfaults, inetd now spews Java mess back to the client.

* Mon Apr 18 2011 Brian Bockelman <bbockelm@cse.unl.edu> 0.3.0-1
- Re-organize code and tighten up visibility rules.

* Mon Apr 18 2011 Brian Bockelman <bbockelm@cse.unl.edu> 0.2.4-1
- Implement "ls" on directories.

* Mon Apr 18 2011 Brian Bockelman <bbockelm@cse.unl.edu> 0.2.3-8
- Fix generation of debuginfo RPM.

* Fri Apr 01 2011 Brian Bockelman <bbockelm@cse.unl.edu> 0.2.3-7
- Destroy HDFS handle only if it was initialized.  Fixes segfault on auth failure.

* Wed Mar 30 2011 Brian Bockelman <bbockelm@cse.unl.edu> 0.2.3-6
- Add explicit dep for globus-common
- Rebuild to trigger a 32-bit build in Koji.

* Mon Jan 31 2011 Brian Bockelman <bbockelm@cse.unl.edu> 0.2.3-4
- Purposefully leak file handle due to lifetime and synchronization issues.

* Fri Dec 17 2010 Brian Bockelman <bbockelm@cse.unl.edu> 0.2.3-2
- Protect against a double-free if only one block is read.

* Wed Dec 15 2010 Brian Bockelman <bbockelm@cse.unl.edu> 0.2.2-1
- Update the bootstrapping scripts for the correct HADOOP_HOME
  and exporting CLASSPATH

* Sun Oct 17 2010 Brian Bockelman <bbockelm@cse.unl.edu> 0.2.1-2
- Update the bootstrapping scripts to include the JVM paths.
- Fix library names for flavourless-Globus

* Fri Oct 15 2010 Brian Bockelman <bbockelm@cse.unl.edu> 0.2.0-3
- Rebuild to create new Koji build.

* Tue Sep 28 2010 Brian Bockelman <bbockelm@cse.unl.edu> 0.2.0-2
- Update configurations to use GT5 GridFTP server from Fedora
- Include environment variables for LCMAPS/LCAS support

* Mon May 17 2010 Brian Bockelman <bbockelm@cse.unl.edu> 0.2.0-1
- Adjust build to depend on hadoop-0.20 RPM layout.
- Commit patches to upstream source.

* Fri Apr 9 2010 Michael Thomas <thomas@hep.caltech.edu> 0.1.0-16
- Add extra debugging lines for file buffer creation to help diagnose
  mmap() failures.  Clean up file buffer if mmap() fails.
- Use MAP_SHARED instead of MAP_PRIVATE to ensure that changes are
  written to disk and memory is not exhausted.

* Wed Feb 10 2010 Michael Thomas <thomas@hep.caltech.edu> 0.1.0-15
- Fix library name on 32-bit arch

* Fri Aug 21 2009 Michael Thomas <thomas@hep.caltech.edu> 0.1.0-14
- Add logrotate configuration file

* Mon Aug 17 2009 Michael Thomas <thomas@hep.caltech.edu> 0.1.0-13
- Add Requires: for osg-ca-certs and fetch-crl
- New upstream source fixing a potential seg fault

* Mon Jul 27 2009 Michael Thomas <thomas@hep.caltech.edu> 0.1.0-12
- Additional debugging lines.
- Add dependency on gratia probes

* Thu Jul 23 2009 Michael Thomas <thomas@hep.caltech.edu> 0.1.0-11
- New upstream sources with syslog fixes

* Sat Jul 4 2009 Michael Thomas <thomas@hep.caltech.edu> 0.1.0-10
- Add GRIDFTP_REPLICA_MAP env var for setting per-file replicas

* Thu Jul 2 2009 Michael Thomas <thomas@hep.caltech.edu> 0.1.0-9
- Move config files to /etc
- Allow local env settings in /etc/gridftp-hdfs/gridftp-hdfs-local.conf

* Thu Jul 2 2009 Michael Thomas <thomas@hep.caltech.edu> 0.1.0-8
- Restart xinetd after installing, but only if xinetd was already running

* Thu Jun 25 2009 Michael Thomas <thomas@hep.caltech.edu> 0.1.0-7
- Add hadoop environment setup to xinetd scripts

* Thu Jun 25 2009 Michael Thomas <thomas@hep.caltech.edu> 0.1.0-6
- Update to latest tarball that contains fixes for the so name

* Thu Jun 25 2009 Michael Thomas <thomas@hep.caltech.edu> 0.1.0-5
- Fix bug in postun
- Add Requires: prima

* Wed Jun 24 2009 Michael Thomas <thomas@hep.caltech.edu> 0.1.0-4
- Add Requires: gpt-postinstall so we know where the globus libraries are
  located
- Update source tarball to pick up xinetd service name change.

* Wed Jun 24 2009 Michael Thomas <thomas@hep.caltech.edu> 0.1.0-3
- Fix paths in inetd scripts
- Add explicit dependency on hadoop

* Wed Jun 24 2009 Michael Thomas <thomas@hep.caltech.edu> 0.1.0-2
- spec file cleanup

* Thu Jun 18 2009 Brian Bockelman <bbockelm@cse.unl.edu> 0.1.0-1
- Creation of GridFTP/HDFS plugin

