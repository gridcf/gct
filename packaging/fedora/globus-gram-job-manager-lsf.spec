Name:		globus-gram-job-manager-lsf
%if %{?suse_version}%{!?suse_version:0} >= 1315
%global apache_license Apache-2.0
%else
%global apache_license ASL 2.0
%endif
%global _name %(tr - _ <<< %{name})
Version:	2.7
Release:	4%{?dist}
Summary:	Grid Community Toolkit - LSF Job Manager

Group:		Applications/Internet
License:	%{apache_license}
URL:		https://github.com/gridcf/gct/
Source:	%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
Obsoletes:      globus-gram-job-manager-setup-lsf < 4.5

Requires:       globus-gram-job-manager-scripts >= 4
Requires:	globus-gass-cache-program >= 5
Requires:	globus-common-progs >= 14
%if 0%{?suse_version} > 0
    %if %{suse_version} < 1140
Requires:     perl = %{perl_version}
    %else
%{perl_requires}
    %endif
%else
Requires:	perl(:MODULE_COMPAT_%(eval "`perl -V:version`"; echo $version))
%endif
BuildRequires:	globus-common-devel >= 14
BuildRequires:	globus-xio-devel >= 3
BuildRequires:	globus-scheduler-event-generator-devel >= 4
BuildRequires:	globus-gram-protocol-devel >= 11
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
BuildRequires:  automake >= 1.11
BuildRequires:  autoconf >= 2.60
BuildRequires:  libtool >= 2.2
%endif
BuildRequires:  pkgconfig

%if %{?suse_version}%{!?suse_version:0} >= 1315
%package -n libglobus_seg_lsf
Summary:        Grid Community Toolkit - LSF Job Manager SEG Module
Group:		Applications/Internet
%endif

%package setup-poll
Summary:        Grid Community Toolkit - LSF Job Manager Setup Files
Group:		Applications/Internet
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6
BuildArch:      noarch
%endif
Provides:       %{name}-setup
Provides:       globus-gram-job-manager-setup
Requires:	%{name} = %{version}-%{release}
requires(post): globus-gram-job-manager-scripts >= 3.4
requires(preun): globus-gram-job-manager-scripts >= 3.4
Conflicts:      %{name}-setup-seg

%package setup-seg
Summary:	Grid Community Toolkit - LSF Job Manager Setup Files
Group:		Applications/Internet
Provides:       %{name}-setup
Provides:       globus-gram-job-manager-setup
Requires:	%{name} = %{version}-%{release}
%if %{?suse_version}%{!?suse_version:0} >= 1315
Requires:	libglobus_seg_lsf = %{version}-%{release}
%endif
Requires:       globus-scheduler-event-generator-progs >= 4
Requires: 	globus-gram-job-manager-scripts >= 4
Requires(post): globus-gram-job-manager-scripts >= 4
Requires(post): globus-scheduler-event-generator-progs >= 4
Requires(preun): globus-gram-job-manager-scripts >= 4
Requires(preun): globus-scheduler-event-generator-progs >= 4
Conflicts:      %{name}-setup-poll

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
LSF Job Manager 

%description setup-poll
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
LSF Job Manager Setup using polling to monitor job state

%if %{?suse_version}%{!?suse_version:0} >= 1315
%description -n libglobus_seg_lsf
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The libglobus_seg_lsf package contains:
LSF Job Manager SEG Module
%endif

%description setup-seg
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
LSF Job Manager Setup using SEG to monitor job state

%prep
%setup -q -n %{_name}-%{version}

%build
%if %{?fedora}%{!?fedora:0} >= 19 || %{?rhel}%{!?rhel:0} >= 7 || %{?suse_version}%{!?suse_version:0} >= 1315
# Remove files that should be replaced during bootstrap
rm -rf autom4te.cache

autoreconf -if
%endif

export MPIEXEC=no
export MPIRUN=no

%configure \
           --disable-static \
           --docdir=%{_docdir}/%{name}-%{version} \
           --includedir=%{_includedir}/globus \
           --with-globus-state-dir=%{_localstatedir}/lib/globus \
           --libexecdir=%{_datadir}/globus \
           --with-perlmoduledir=%{perl_vendorlib}

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT
# Remove jobmanager-lsf from install dir so that it can be
# added/removed by post scripts
rm $RPM_BUILD_ROOT/etc/grid-services/jobmanager-lsf

# Remove libtool archives (.la files)
find $RPM_BUILD_ROOT%{_libdir} -name 'lib*.la' -exec rm -v '{}' \;
# Remove pkg-config files (.pc files)
find $RPM_BUILD_ROOT%{_libdir} -name '*.pc' -exec rm -v '{}' \;

%check
make %{?_smp_mflags} check

%post setup-poll
if [ $1 -eq 1 ]; then
    globus-gatekeeper-admin -e jobmanager-lsf-poll -n jobmanager-lsf > /dev/null 2>&1 || :
    if [ ! -f /etc/grid-services/jobmanager ]; then
        globus-gatekeeper-admin -e jobmanager-lsf-poll -n jobmanager
    fi
fi

%preun setup-poll
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-lsf-poll > /dev/null 2>&1 || :
fi

%postun setup-poll
if [ $1 -eq 1 ]; then
    globus-gatekeeper-admin -e jobmanager-lsf-poll -n jobmanager-lsf > /dev/null 2>&1 || :
elif [ $1 -eq 0 -a ! -f /etc/grid-services/jobmanager ]; then
    globus-gatekeeper-admin -E > /dev/null 2>&1 || :
fi

%if %{?suse_version}%{!?suse_version:0} >= 1315
%post -n libglobus_seg_lsf
ldconfig

%postun -n libglobus_seg_lsf
ldconfig
%endif

%post setup-seg
%if %{?suse_version}%{!?suse_version:0} == 0
ldconfig
%endif
if [ $1 -eq 1 ]; then
    globus-gatekeeper-admin -e jobmanager-lsf-seg -n jobmanager-lsf > /dev/null 2>&1 || :
    globus-scheduler-event-generator-admin -e lsf > /dev/null 2>&1 || :
    service globus-scheduler-event-generator condrestart lsf
fi

%preun setup-seg
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-lsf-seg > /dev/null 2>&1 || :
    globus-scheduler-event-generator-admin -d lsf > /dev/null 2>&1 || :
    service globus-scheduler-event-generator stop lsf > /dev/null 2>&1 || :
fi

%postun setup-seg
%if %{?suse_version}%{!?suse_version:0} == 0
ldconfig
%endif
if [ $1 -eq 1 ]; then
    globus-gatekeeper-admin -e jobmanager-lsf-seg > /dev/null 2>&1 || :
    globus-scheduler-event-generator-admin -e lsf > /dev/null 2>&1 || :
    service globus-scheduler-event-generator condrestart lsf > /dev/null 2>&1 || :
elif [ $1 -eq 0 -a ! -f /etc/grid-services/jobmanager ]; then
    globus-gatekeeper-admin -E > /dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}
%{_docdir}/%{name}-%{version}/GLOBUS_LICENSE
%dir %{_sysconfdir}/globus
%config(noreplace) %{_sysconfdir}/globus/globus-lsf.conf
%dir %{_datadir}/globus/globus_gram_job_manager
%{_datadir}/globus/globus_gram_job_manager/lsf.rvf
%dir %{perl_vendorlib}/Globus/GRAM/JobManager
%{perl_vendorlib}/Globus/GRAM/JobManager/lsf.pm

%files setup-poll
%defattr(-,root,root,-)
%dir %{_sysconfdir}/grid-services
%dir %{_sysconfdir}/grid-services/available
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-lsf-poll

%if %{?suse_version}%{!?suse_version:0} >= 1315
%files -n libglobus_seg_lsf
%defattr(-,root,root,-)
%{_libdir}/libglobus*
%endif

%files setup-seg
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-lsf-seg
%dir %{_sysconfdir}/globus/scheduler-event-generator
%dir %{_sysconfdir}/globus/scheduler-event-generator/available
%{_sysconfdir}/globus/scheduler-event-generator/available/lsf
%if %{?suse_version}%{!?suse_version:0} == 0
%{_libdir}/libglobus*
%endif

%changelog
* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 2.7-4
- Rebuild after changes for el.5 with openssl101e

* Thu Aug 25 2016 Globus Toolkit <support@globus.org> - 2.7-3
- Updates for SLES 12

* Sat Aug 20 2016 Globus Toolkit <support@globus.org> - 2.7-1
- Update bug report URL

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 2.6-2
- Add vendor

* Mon Sep 22 2014 Globus Toolkit <support@globus.org> - 2.6-1
- Remove unused Doxyfile
- Quiet some autoconf/automake warnings

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 2.5-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 2.4-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 2.4-1
- Merge changes from Mattias Ellert

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 2.3-1
- Version bump for consistency

* Thu Feb 13 2014 Globus Toolkit <support@globus.org> - 2.2-1
- Packaging Fixes

* Tue Jan 21 2014 Globus Toolkit <support@globus.org> - 2.1-1
- Repackage for GT6 without GPT

* Thu Oct 10 2013 Globus Toolkit <support@globus.org> - 1.2-1
- GT-344: Cut and past error in gpt metadata for GRAM LSF module

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 1.1-2
- GT-424: New Fedora Packaging Guideline - no %_isa in BuildRequires

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 1.1-1
- 5.2.3

* Fri Aug 17 2012 Joseph Bester <bester@mcs.anl.gov> - 1.0-1
- Initial packaging
