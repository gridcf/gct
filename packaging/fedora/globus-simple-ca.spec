%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		globus-simple-ca
%global _name %(echo %{name} | tr - _)
Version:	5.1
Release:	1%{?dist}
Summary:	Grid Community Toolkit - Simple CA Utility

Group:		Applications/Internet
License:	%{?suse_version:Apache-2.0}%{!?suse_version:ASL 2.0}
URL:		https://github.com/gridcf/gct/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildArch:	noarch

BuildRequires:	openssl
#		Additional requirements for make check
BuildRequires:	globus-common-progs >= 14
BuildRequires:	perl(Test::More)

Requires:	globus-common-progs >= 14
Requires:	globus-gsi-cert-utils-progs
Requires:	openssl
%if %{?suse_version}%{!?suse_version:0}
Requires(pre):	shadow
%else
Requires(pre):	shadow-utils
%endif
Requires(post):	globus-common-progs >= 14
Requires(post):	globus-gsi-cert-utils-progs
Requires(post):	openssl

%description
The Grid Community Toolkit (GCT) is an open source software toolkit used for
building grid systems and applications. It is a fork of the Globus Toolkit
originally created by the Globus Alliance. It is supported by the Grid
Community Forum (GridCF) that provides community-based support for core
software packages in grid computing.

The %{name} package contains:
Simple CA Utility

%prep
%setup -q -n %{_name}-%{version}

%build
%configure --disable-static \
	   --includedir=%{_includedir}/globus \
	   --libexecdir=%{_datadir}/globus \
	   --docdir=%{_pkgdocdir}

make %{?_smp_mflags}

%install
make install DESTDIR=$RPM_BUILD_ROOT

%check
make %{?_smp_mflags} check VERBOSE=1

%pre
getent group simpleca >/dev/null || groupadd -r simpleca
getent passwd simpleca >/dev/null || \
useradd -r -g simpleca -d %{_localstatedir}/lib/globus/simple_ca \
   -s /sbin/nologin \
   -c "User to run the SimpleCA" simpleca
exit 0

%post
simplecadir=%{_localstatedir}/lib/globus/simple_ca
mkdir -p ${simplecadir}
if [ ! -f ${simplecadir}/cacert.pem ] ; then
    tempdir=$(mktemp -d)
    cd "$tempdir"
    grid-ca-create -noint -nobuild -dir "${simplecadir}"
    (umask 077; echo globus > ${simplecadir}/passwd)
    simplecahash=`openssl x509 -hash -noout -in ${simplecadir}/cacert.pem`
    cd $simplecadir
    grid-ca-package -cadir ${simplecadir}
    tar --strip 1 --no-same-owner -zx --exclude debian -C /etc/grid-security/certificates -f ${simplecadir}/globus_simple_ca_$simplecahash.tar.gz
    chown -R simpleca:simpleca ${simplecadir}
    chmod -R g+rw ${simplecadir}
    find ${simplecadir} -type d -exec chmod g+xs {} \;
    if [ ! -r /etc/grid-security/globus-user-ssl.conf ]; then
	grid-default-ca -ca $simplecahash
    fi
    if [ ! -f /etc/grid-security/hostcert.pem ] && \
       [ ! -f /etc/grid-security/hostcert_request.pem ] && \
       [ ! -f /etc/grid-security/hostkey.pem ]; then
	grid-cert-request -cn `hostname -f` -host `hostname -f`
	su -s /bin/sh simpleca -c "umask 007; grid-ca-sign \
	   -in /etc/grid-security/hostcert_request.pem \
	   -out ${simplecadir}/hostcert.pem"
	cp "${simplecadir}/hostcert.pem" /etc/grid-security/hostcert.pem
    fi
    cd -
    rm -rf "$tempdir"
fi

%files
%defattr(-,root,root,-)
%{_bindir}/grid-ca-create
%{_bindir}/grid-ca-package
%{_bindir}/grid-ca-sign
%doc %{_mandir}/man1/grid-ca-create.1*
%doc %{_mandir}/man1/grid-ca-package.1*
%doc %{_mandir}/man1/grid-ca-sign.1*
%dir %{_pkgdocdir}
%doc %{_pkgdocdir}/GLOBUS_LICENSE

%changelog
* Thu Apr 25 2019 Mattias Ellert <mattias.ellert@physics.uu.se> - 5.1-1
- Fix openssl output format issue (incomplete openssl 1.1 port)

* Sat Mar 31 2018 Mattias Ellert <mattias.ellert@physics.uu.se> - 5.0-1
- First Grid Community Toolkit release

* Fri May 12 2017 Globus Toolkit <support@globus.org> - 4.24-4
- Stop leaving files in /

* Thu Sep 08 2016 Globus Toolkit <support@globus.org> - 4.24-3
- Update for el.5 openssl101e, replace docbook with asciidoc

* Fri Aug 26 2016 Globus Toolkit <support@globus.org> - 4.23-4
- Updates for SLES 12

* Tue Aug 16 2016 Globus Toolkit <support@globus.org> - 4.23-1
- Updates for OpenSSL 1.1.0

* Thu Aug 06 2015 Globus Toolkit <support@globus.org> - 4.22-2
- Add vendor

* Tue Jul 28 2015 Globus Toolkit <support@globus.org> - 4.22-1
- Fix test case for ca key bits

* Fri Jul 24 2015 Globus Toolkit <support@globus.org> - 4.21-1
- GT-617: Use 4096-bit RSA key for globus-simple-ca

* Thu May 28 2015 Globus Toolkit <support@globus.org> - 4.20-1
- Increase default key size

* Thu Apr 02 2015 Globus Toolkit <support@globus.org> - 4.19-1
- Add support for additional DN components

* Tue Nov 18 2014 Globus Toolkit <support@globus.org> - 4.18-1
- Avoid make check failures when ~/.rnd is not writable

* Mon Nov 10 2014 Globus Toolkit <support@globus.org> - 4.17-1
- Fix regression writing to cert dir

* Mon Nov 10 2014 Globus Toolkit <support@globus.org> - 4.16-1
- Fix regression writing to cert dir

* Mon Nov 03 2014 Globus Toolkit <support@globus.org> - 4.15-1
- only check for writable dir

* Fri Aug 22 2014 Globus Toolkit <support@globus.org> - 4.14-1
- Merge fixes from ellert-globus_6_branch

* Wed Aug 20 2014 Globus Toolkit <support@globus.org> - 4.13-2
- Fix Source path

* Mon Jun 09 2014 Globus Toolkit <support@globus.org> - 4.13-1
- Merge changes from Mattias Ellert

* Mon Apr 21 2014 Globus Toolkit <support@globus.org> - 4.12-1
- Test fixes

* Fri Apr 18 2014 Globus Toolkit <support@globus.org> - 4.11-1
- Version bump for consistency

* Thu Feb 27 2014 Globus Toolkit <support@globus.org> - 4.10-1
- Packaging fixes, Warning Cleanup

* Tue Feb 25 2014 Globus Toolkit <support@globus.org> - 4.9-1
- Packaging fixes

* Wed Feb 19 2014 Globus Toolkit <support@globus.org> - 4.8-1
- Packaging fixes

* Tue Feb 18 2014 Globus Toolkit <support@globus.org> - 4.7-1
- Test fixes

* Tue Feb 18 2014 Globus Toolkit <support@globus.org> - 4.6-1
- Packaging fixes

* Tue Feb 18 2014 Globus Toolkit <support@globus.org> - 4.5-1
- Test fixes

* Tue Feb 18 2014 Globus Toolkit <support@globus.org> - 4.4-1
- Test fixes

* Tue Feb 18 2014 Globus Toolkit <support@globus.org> - 4.3-1
- Packaging fixes

* Tue Feb 18 2014 Globus Toolkit <support@globus.org> - 4.4-1
- Packaging fixes

* Tue Feb 18 2014 Globus Toolkit <support@globus.org> - 4.3-1
- Don't depend on finding initializer/args parser

* Tue Feb 18 2014 Globus Toolkit <support@globus.org> - 4.2-1
- Test fixes

* Tue Feb 18 2014 Globus Toolkit <support@globus.org> - 4.1-1
- Packaging fixes

* Wed Jan 22 2014 Globus Toolkit <support@globus.org> - 4.0-1
- Repackage for GT6 without GPT

* Thu Oct 10 2013 Globus Toolkit <support@globus.org> - 3.5-1
- GT-405: Non-portable use of echo in shell script

* Fri Sep 13 2013 Globus Toolkit <support@globus.org> - 3.4-4
- Don't die in postinst if domain name is not qualified

* Mon Jul 08 2013 Globus Toolkit <support@globus.org> - 3.4-3
- license is not a dir

* Wed Jun 26 2013 Globus Toolkit <support@globus.org> - 3.4-2
- GT-424: New Fedora Packaging Guideline - no %%_isa in BuildRequires

* Fri May 24 2013 Globus Toolkit <support@globus.org> - 3.4-1
- Fix test for absolute path on some versions of expr

* Wed Feb 13 2013 Globus Toolkit <support@globus.org> - 3.3-1
- GT-362: simple ca loses spaces in dn in signing policy

* Mon Nov 26 2012 Globus Toolkit <support@globus.org> - 3.2-2
- 5.2.3

* Mon Oct 29 2012 Joseph Bester <bester@mcs.anl.gov> - 3.2-1
- GT-312: automate native simple_ca package more

* Mon Jul 16 2012 Joseph Bester <bester@mcs.anl.gov> - 3.1-3
- GT 5.2.2 final

* Fri Jun 29 2012 Joseph Bester <bester@mcs.anl.gov> - 3.1-2
- GT 5.2.2 Release

* Tue May 22 2012 Joseph Bester <bester@mcs.anl.gov> - 3.1-1
- GT-151: Build RPMS for SuSE 11

* Wed May 09 2012 Joseph Bester <bester@mcs.anl.gov> - 3.0-7
- RHEL 4 patches

* Tue Feb 14 2012 Joseph Bester <bester@mcs.anl.gov> - 3.0-6
- Updated version numbers

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 3.0-5
- Update for 5.2.0 release

* Mon Dec 05 2011 Joseph Bester <bester@mcs.anl.gov> - 3.0-4
- Last sync prior to 5.2.0

* Tue Oct 11 2011 Joseph Bester <bester@mcs.anl.gov> - 3.0-3
- Add explicit dependencies on >= 5.2 libraries

* Thu Sep 01 2011 Joseph Bester <bester@mcs.anl.gov> - 3.0-2
- Update for 5.1.2 release

* Tue Mar 29 2011 - 2.0-1
- Initial version
