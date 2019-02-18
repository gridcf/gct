%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-gram-job-manager
%global _name %(tr - _ <<< %{name})
Version:	15.4
Release:	1%{?dist}
Summary:	Grid Community Toolkit - GRAM Jobmanager

Group:		Applications/Internet
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	gcc
BuildRequires:	globus-common-devel >= 15
BuildRequires:	globus-gsi-credential-devel >= 5
BuildRequires:	globus-gass-cache-devel >= 8
BuildRequires:	globus-gass-transfer-devel >= 7
BuildRequires:	globus-gram-protocol-devel >= 11
BuildRequires:	globus-gssapi-gsi-devel >= 10
BuildRequires:	globus-gss-assist-devel >= 8
BuildRequires:	globus-gsi-sysconfig-devel >= 5
BuildRequires:	globus-callout-devel >= 2
BuildRequires:	globus-xio-devel >= 3
BuildRequires:	globus-xio-popen-driver-devel >= 2
BuildRequires:	globus-rsl-devel >= 9
BuildRequires:	globus-gram-job-manager-callout-error-devel >= 2
BuildRequires:	globus-scheduler-event-generator-devel >= 4
%if %{?suse_version}%{!?suse_version:0}
BuildRequires:	libopenssl-devel
%else
BuildRequires:	openssl-devel
%endif
BuildRequires:	libxml2-devel
#		Additional requirements for make check
BuildRequires:	globus-io-devel >= 9
BuildRequires:	globus-gram-client-devel >= 3
BuildRequires:	globus-gass-server-ez-devel >= 2
BuildRequires:	globus-common-progs >= 15
BuildRequires:	globus-gatekeeper >= 9
BuildRequires:	globus-gram-client-tools >= 10
BuildRequires:	globus-gass-copy-progs >= 8
BuildRequires:	globus-gass-cache-program >= 5
BuildRequires:	globus-gram-job-manager-scripts >= 6
BuildRequires:	globus-proxy-utils >= 5
BuildRequires:	globus-gsi-cert-utils-progs
BuildRequires:	globus-gram-job-manager-fork-setup-poll
BuildRequires:	openssl
BuildRequires:	perl(Test)
BuildRequires:	perl(Test::More)

%if %{?suse_version}%{!?suse_version:0}
%global libpkg libglobus_seg_job_manager
%else
%global libpkg globus-seg-job-manager
%endif

Requires:	globus-xio-popen-driver%{?_isa} >= 2
Requires:	globus-common-progs >= 15
Requires:	globus-gatekeeper >= 9
Requires:	globus-gram-client-tools >= 10
Requires:	globus-gass-copy-progs >= 8
Requires:	globus-gass-cache-program >= 5
Requires:	globus-gram-job-manager-scripts >= 6
Requires:	globus-proxy-utils >= 5
Requires:	globus-gsi-cert-utils-progs
Requires:	%{libpkg}%{?_isa} = %{version}-%{release}
Obsoletes:	%{name}-doc < 15

%package -n %{libpkg}
Summary:	Grid Community Toolkit - Scheduler Event Generator Job Manager
Group:		Applications/Internet
Requires:	%{name} = %{version}-%{release}
%if %{?suse_version}%{!?suse_version:0}
Provides:	globus-seg-job-manager = %{version}-%{release}
Obsoletes:	globus-seg-job-manager < %{version}-%{release}
%endif

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
GRAM Jobmanager

%description -n %{libpkg}
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{libpkg} package contains:
Scheduler Event Generator Job Manager

%prep
%setup -q -n %{_name}-%{version}

%build
export GLOBUS_VERSION=6.2
%configure --disable-static \
	   --includedir=%{_includedir}/globus \
	   --libexecdir=%{_datadir}/globus \
	   --docdir=%{_pkgdocdir}

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT

# Remove libtool archives (.la files)
rm $RPM_BUILD_ROOT%{_libdir}/*.la

%if %{?rhel}%{!?rhel:0} == 6
# Remove su option from logrotate file in EPEL 6 (not supported)
sed '/ su /d' -i $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/globus-job-manager
%endif

%check
GLOBUS_HOSTNAME=localhost make %{?_smp_mflags} check VERBOSE=1

%post -n %{libpkg} -p /sbin/ldconfig

%postun -n %{libpkg} -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_bindir}/globus-personal-gatekeeper
%{_sbindir}/globus-gram-streamer
%{_sbindir}/globus-job-manager
%{_sbindir}/globus-job-manager-lock-test
%{_sbindir}/globus-rvf-check
%{_sbindir}/globus-rvf-edit
%dir %{_datadir}/globus
%dir %{_datadir}/globus/%{_name}
%{_datadir}/globus/%{_name}/globus-gram-job-manager.rvf
%config(noreplace) %{_sysconfdir}/logrotate.d/globus-job-manager
%dir %{_localstatedir}/lib/globus
%dir %{_localstatedir}/lib/globus/gram_job_state
%dir %{_localstatedir}/log/globus
%dir %{_sysconfdir}/globus
%config(noreplace) %{_sysconfdir}/globus/globus-gram-job-manager.conf
%doc %{_mandir}/man1/globus-personal-gatekeeper.1*
%doc %{_mandir}/man5/rsl.5*
%doc %{_mandir}/man8/globus-job-manager.8*
%doc %{_mandir}/man8/globus-rvf-check.8*
%doc %{_mandir}/man8/globus-rvf-edit.8*
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%files -n %{libpkg}
%defattr(-,root,root,-)
# This is a loadable module (plugin)
%{_libdir}/libglobus_seg_job_manager.so

%changelog
* Fri Feb 15 2019 Mattias Ellert <mattias.ellert@physics.uu.se> - 15.4-1
- Add su option to logrotate file

* Fri Dec 07 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 15.3-1
- Remove usage statistics collection support

* Wed Nov 21 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 15.2-1
- Doxygen fixes

* Mon Nov 05 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 15.1-2
- Bump GCT release version to 6.2

* Sat May 05 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 15.1-1
- Use 2048 bit RSA key for tests

* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 15.0-1
- First Grid Community Toolkit release
- Remove support for openssl101e (RHEL5 is EOL)
- Disable usage statistics reporting by default
- Drop documentation package (move rsl.5 man page to main package)
- Move globus-seg-job-manager plugin to a separate package

* Sat Jan 20 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 14.37-1
- Workaround non-implemented features on GNU/Hurd (socket buffer size)
- Move grid-proxy-destroy call to before starting personal gatekeeper in
  the test wrapper

* Tue Apr 25 2017 Globus Toolkit <support@globus.org> - 14.36-1
- Default to running personal gatekeeper on an ephemeral port

* Fri Sep 09 2016 Globus Toolkit <support@globus.org> - 14.35-1
- Update for el.5 openssl101e

* Fri Sep 09 2016 Globus Toolkit <support@globus.org> - 14.34-1
- Updates to rebuild el.5 with openssl101e

* Tue Sep 06 2016 Globus Toolkit <support@globus.org> - 14.33-3
- Fix issue #71: globus-gram-job-manager test leaves process behind

* Wed Aug 31 2016 Globus Toolkit <support@globus.org> - 14.32-1
- Skip some tests as root

* Tue Aug 30 2016 Globus Toolkit <support@globus.org> - 14.31-2
- Updates for SLES 12

* Tue Aug 30 2016 Globus Toolkit <support@globus.org> - 14.31-1
- Set test case cache dir

* Fri Aug 19 2016 Globus Toolkit <support@globus.org> - 14.30-1
- Fix test problem in el5 mock with HOME=/builddir and root user

* Thu Aug 18 2016 Globus Toolkit <support@globus.org> - 14.29-1
- Makefile fix

* Tue Aug 16 2016 Globus Toolkit <support@globus.org> - 14.28-1
- Updates for OpenSSL 1.1.0

* Mon May 23 2016 Globus Toolkit <support@globus.org> - 14.27-3
- Add perl-Test dependency for fedora 24

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 14.27-2
- Add vendor

* Tue Jul 28 2015 Globus Toolkit <support@globus.org> - 14.27-1
- GT-619: Uninitialized data in job manager cause crash

* Thu Jun 18 2015 Globus Toolkit <support@globus.org> - 14.26-1
- Convert manpage source to asciidoc
- Fix GT-590: GT5 shows running jobs as being in pending state

* Fri Apr 17 2015 Globus Toolkit <support@globus.org> - 14.25-2
- Add build dependency on perl-Test-Simple

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 14.25-1
- don't use $HOME in tests

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 14.24-1
- globus-personal-gatekeeper cleanups

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 14.23-1
- doxygen fixes

* Wed Oct 22 2014 Globus Toolkit <support@globus.org> - 14.22-2
- Build dependency on ltdl for tests

* Thu Sep 25 2014 Globus Toolkit <support@globus.org> - 14.22-1
- Fix some documentation typos

* Thu Sep 18 2014 Globus Toolkit <support@globus.org> - 14.21-1
- GT-455: Incorporate OSG patches
- GT-456: OSG patch "load_requests_before_activating_socket.patch" for globus-gram-job-manager
- GT-466: OSG patch "logrotate-copytruncate-jobmanager.patch" for globus-gram-job-manager
- Fix test crash with odd dns responder

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 14.20-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 14.19-2
- Fix Source path

* Wed Aug 06 2014 Globus Toolkit <support@globus.org> - 14.19-1
- Fix crash when non-standard USER environment variable is not set

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 14.18-1
- Merge changes from Mattias Ellert

* Tue May 27 2014 Globus Toolkit <support@globus.org> - 14.17-1
- Fix path to scripts for tests

* Thu May 08 2014 Globus Toolkit <support@globus.org> - 14.16-1
- Unset proxy in tests

* Thu May 08 2014 Globus Toolkit <support@globus.org> - 14.15-1
- Create proxy

* Wed May 07 2014 Globus Toolkit <support@globus.org> - 14.14-1
- Don't use default proxy if available

* Tue May 06 2014 Globus Toolkit <support@globus.org> - 14.13-1
- Add TAP prefix to test output

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 14.12-1
- Packaging fixes

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 14.11-1
- Packaging fixes

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 14.10-1
- Packaging fixes

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 14.9-1
- Packaging fixes

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 14.8-1
- Packaging fixes

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 14.7-1
- Packaging fixes

* Fri Apr 25 2014 Globus Toolkit <support@globus.org> - 14.6-1
- Packaging fixes

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 14.5-1
- Version bump for consistency

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 14.4-1
- Version bump for consistency

* Thu Feb 27 2014 Globus Toolkit <support@globus.org> - 14.3-1
- Packaging fixes, Warning Cleanup

* Fri Feb 21 2014 Globus Toolkit <support@globus.org> - 14.2-1
- Packaging fixes

* Wed Jan 22 2014 Globus Toolkit <support@globus.org> - 14.1-1
- Repackage for GT6 without GPT

* Wed Jan 22 2014 Globus Toolkit <support@globus.org> - 14.0-1
- Repackage for GT6 without GPT

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 13.53-2
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Thu May 16 2013 Globus Toolkit <support@globus.org> - 13.53-1
- GT-311: globus job manager is leaking memory

* Wed Apr 10 2013 Globus Toolkit <support@globus.org> - 13.52-1
- GT-384: GRAM mishandles long script responses

* Wed Feb 20 2013 Globus Toolkit <support@globus.org> - 13.51-3
- Workaround missing F18 doxygen/latex dependency

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 13.51-2
- 5.2.3

* Fri Oct 19 2012 Globus Toolkit <support@globus.org> - 13.51-1
- GT-291: Reduce verbosity of INFO level debug log on GRAM

* Thu Oct 11 2012 Globus Toolkit <support@globus.org> - 13.50-1
- GT-298: Leading whitespace confuses rvf parser

* Fri Aug 17 2012 Joseph Bester <bester@mcs.anl.gov> - 13.49-1
- GT-268: GRAM job manager seg module fails to replay first log of the month on restart
- GT-270: job manager crash at shutdown (extra_envvar free)

* Tue Jul 17 2012 Joseph Bester <bester@mcs.anl.gov> - 13.48-1
- GT-253: gatekeeper and job manager don't build on hurd

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 13.47-3
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 13.47-2
- GT 5.2.2 Release

* Mon Jun 25 2012 Joseph Bester <bester@mcs.anl.gov> - 13.47-1
- GT-212: Missing debian packages

* Mon Jun 18 2012 Joseph Bester <bester@mcs.anl.gov> - 13.46-1
- GT-224: Manage GRAM execution per client host for scalability for different clients

* Wed Jun 13 2012 Joseph Bester <bester@mcs.anl.gov> - 13.45-1
- GT-225: GRAM5 skips some SEG events

* Wed Jun 06 2012 Joseph Bester <bester@mcs.anl.gov> - 13.44-1
- GT-157: Hash gram_job_state directory by user

* Fri Jun 01 2012 Joseph Bester <bester@mcs.anl.gov> - 13.43-1
- GT-214: Leaks in the job manager restart code

* Thu May 24 2012 Joseph Bester <bester@mcs.anl.gov> - 13.42-1
- GT-209: job manager crash in query

* Tue May 22 2012 Joseph Bester <bester@mcs.anl.gov> - 13.41-1
- GT-199: GRAM audit checks result username incorrectly
- GT-192: Segfault in globus-gram-streamer

* Fri May 18 2012 Joseph Bester <bester@mcs.anl.gov> - 13.40-1
- GT-149: Memory leaks in globus-job-manager
- GT-186: GRAM job manager leaks condor log path
- GT-187: GRAM job manager leaks during stdio update
- GT-189: GRAM job manager regular expression storage grows
- GT-190: GRAM job manager leaks callback contact

* Fri May 11 2012 Joseph Bester <bester@mcs.anl.gov> - 13.38-1
- GT-185: globus-personal-gatekeeper creates too-long paths on MacOS

* Fri May 11 2012 Joseph Bester <bester@mcs.anl.gov> - 13.37-1
- GT-65: GRAM records datagram socket failure, but doesn't record socket name

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 13.36-1
- GRAM-288: Kill off perl processes when idle

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 13.35-3
- RHEL 4 patches

* Fri May 04 2012 Joseph Bester <bester@mcs.anl.gov> - 13.35-2
- SLES 11 patches

* Thu May 03 2012 Joseph Bester <bester@mcs.anl.gov> - 13.35-1
- GRAM-329: Condor fake-SEG loses track of job
- GRAM-345: Job manager deletes job dir sometimes

* Wed Apr 11 2012 Joseph Bester <bester@mcs.anl.gov> - 13.33-1
- GRAM-334: job manager doesn't work if unix socket path is too long
- GRAM-338: GRAM job manager mishandles peer name when proxying messages through the gatekeeper
- GRAM-340: job manager crashes during stdio size query
- GRAM-342: intra-job manager protocol doesn't keep do signal-safe reads

* Mon Apr 02 2012 Joseph Bester <bester@mcs.anl.gov> - 13.31-1
- GRAM-329: Condor fake-SEG loses track of job

* Thu Mar 29 2012 Joseph Bester <bester@mcs.anl.gov> - 13.30-1
- GRAM-327: list default values for RSL attributes

* Wed Mar 28 2012 Joseph Bester <jbester@mcs.anl.gov> - 13.29-1
- GRAM-330: Buffer overflow in globus_gram_job_manager_seg_parse_condor_id

* Tue Mar 27 2012 Joseph Bester <bester@mcs.anl.gov> - 13.28-1
- GRAM-321: globus-job-manager emits warning about all jobs on restart
- GRAM-323: RVF parser leaks file descriptors
- GRAM-326: Can't renew job proxy after GLOBUS_GRAM_PROTOCOL_ERROR_COMMIT_TIMED_OUT error
- GRAM-328: job manager waits for two-phase delay when stopping

* Thu Mar 22 2012 Joseph Bester <jbester@mcs.anl.gov> - 13.27-1
- GRAM-325: job manager crashes when reading empty condor log

* Wed Mar 14 2012 Joseph Bester <bester@mcs.anl.gov> - 13.26-1
- GRAM-314: Jobmanager locking protocol doesn't handle deletion of lockfiles

* Wed Mar 14 2012 Joseph Bester <bester@mcs.anl.gov> - 13.25-1
- GRAM-273: Crufty Condor logs can cause major performance hit
- GRAM-306: Job Manager stdio_size query logging crash
- GRAM-315: Job locking doesn't handle ENOENT gracefully
- GRAM-317: job manager fails transferring job between processes if the proxy is larger than the socket buffer

* Thu Mar 1 2012 Joseph Bester <bester@mcs.anl.gov> - 13.22-1
- RIC-239: GSSAPI Token inspection fails when using TLS 1.2

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 13.21-1
- GRAM-272: Allow site-specific RVF entries
- GRAM-294: GRAM should clean up files better
- GRAM-305: Jobmanager reporting DONE status when stage-out failed
- RIC-226: Some dependencies are missing in GPT metadata

* Thu Dec 22 2011 Joseph Bester <bester@mcs.anl.gov> - 13.19-1
- GRAM-232: Incorrect directory permissions cause an infinite loop
- GRAM-302: Incorrect error when state file write fails
- GRAM-301: GRAM validation file parser doesn't handle empty quoted values
	    correctly
- GRAM-300: GRAM job manager doxygen refers to obsolete command-line options
- GRAM-299: Not all job log messages obey loglevel RSL attribute
- GRAM-296: Compile Failure on Solaris

* Thu Dec 08 2011 Joseph Bester <bester@mcs.anl.gov> - 13.14-1
- Fix some cases of multiple submits of a GRAM job to condor

* Wed Dec 07 2011  <bester@centos55.local> - 13.13-1
- GRAM-292: GRAM crashes when parsing partial condor log

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 13.12-2
- Update for 5.2.0 release

* Thu Dec 01 2011 Joseph Bester <bester@mcs.anl.gov> - 13.12-1
- GRAM-289: GRAM jobs resubmitted

* Mon Nov 28 2011 Joseph Bester <bester@mcs.anl.gov> - 13.11-1
- GRAM-286: Set default jobmanager log in native packages
- Add gatekeeper and psmisc dependencies

* Mon Nov 21 2011 Joseph Bester <bester@mcs.anl.gov> - 13.10-1
- GRAM-282: Add hooks to job manager to handle log rotation

* Mon Nov 14 2011 Joseph Bester <bester@mcs.anl.gov> - 13.9-1
- GRAM-271: GRAM Condor polling overpolls

* Mon Nov 07 2011 Joseph Bester <bester@mcs.anl.gov> - 13.8-1
- GRAM-268: GRAM requires gss_export_sec_context to work

* Fri Oct 28 2011 Joseph Bester <bester@mcs.anl.gov> - 13.7-1
- GRAM-266: Do not issue "Error locking file" warning if another jobmanager
  exists

* Wed Oct 26 2011 Joseph Bester <bester@mcs.anl.gov> - 13.6-1
- GRAM-265: GRAM logging.c sets FD_CLOEXEC incorrectly

* Mon Oct 24 2011 Joseph Bester <bester@mcs.anl.gov> - 13.5-2
- set aclocal_includes="-I ." prior to bootsrap

* Thu Oct 20 2011 Joseph Bester <bester@mcs.anl.gov> - 13.5-1
- GRAM-227: Manager double-locked

* Tue Oct 18 2011 Joseph Bester <bester@mcs.anl.gov> - 13.4-1
- GRAM-262: job manager -extra-envvars implementation doesn't match description

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 13.3-2
- Add explicit dependencies on >= 5.2 libraries

* Tue Oct 04 2011 Joseph Bester <bester@mcs.anl.gov> - 13.3-1
- GRAM-240: globus_xio_open in script code can recurse

* Thu Sep 22 2011  <bester@mcs.anl.gov> - 13.2-1
- GRAM-257: Set default values for GLOBUS_GATEKEEPER_*

* Thu Sep 22 2011 Joseph Bester <bester@mcs.anl.gov> - 13.1-1
- Fix: GRAM-250

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 13.0-2
- Update for 5.1.2 release

* Sun Jun 05 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 10.70-1
- Update to Globus Toolkit 5.0.4
- Fix doxygen markup

* Mon Apr 25 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 10.67-3
- Add README file

* Tue Apr 19 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 10.67-2
- Updated patch

* Thu Feb 24 2011 Mattias Ellert <mattias.ellert@fysast.uu.se> - 10.67-1
- Update to Globus Toolkit 5.0.3

* Tue Feb 08 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 10.59-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_15_Mass_Rebuild

* Sun Jul 18 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 10.59-2
- Move client and server man pages to main package

* Sat Jul 17 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 10.59-1
- Update to Globus Toolkit 5.0.2

* Sat Jun 05 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 10.42-2
- Additional portability fixes

* Wed Apr 14 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 10.42-1
- Update to Globus Toolkit 5.0.1

* Sat Jan 23 2010 Mattias Ellert <mattias.ellert@fysast.uu.se> - 10.17-1
- Update to Globus Toolkit 5.0.0

* Thu Jul 30 2009 Mattias Ellert <mattias.ellert@fysast.uu.se> - 8.15-1
- Autogenerated
