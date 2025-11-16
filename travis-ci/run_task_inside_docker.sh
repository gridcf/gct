#!/bin/bash

TRAVISUID=$1
TASK=$2
COMPONENTS=${3-}

[[ $TASK == noop ]] && exit 0

set -e

case $(</etc/redhat-release) in

    CentOS\ Stream*\ 9*)

        OS=centos-stream-9
        release_ver=el9
        ;;

    CentOS\ Stream*\ 10*)

        OS=centos-stream-10
        release_ver=el10
        ;;

    Rocky\ Linux*\ 8*)

        OS=rockylinux8
        release_ver=el8
        ;;

    Rocky\ Linux*\ 9*)

        OS=rockylinux9
        release_ver=el9
        ;;

    Rocky\ Linux*\ 10*)

        OS=rockylinux10
        release_ver=el10
        ;;

    *) OS=unknown ;;
esac

# EPEL required for UDT
case $OS in
    # from `https://docs.fedoraproject.org/en-US/epel/#_quickstart`
    rockylinux8)
              dnf -y install dnf-plugins-core
              dnf config-manager --set-enabled powertools
              dnf -y install epel-release
              ;;
    rockylinux9)
              dnf -y install dnf-plugins-core
              dnf config-manager --set-enabled crb
              dnf -y install epel-release
              ;;
    rockylinux10)
              dnf -y install dnf-plugins-core
              dnf config-manager --set-enabled crb
              dnf -y install epel-release
              ;;
    centos-stream-9)
              dnf -y install dnf-plugins-core
              dnf config-manager --set-enabled crb
              dnf -y install \
                https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm \
                https://dl.fedoraproject.org/pub/epel/epel-next-release-latest-9.noarch.rpm
              ;;
    centos-stream-10)
              dnf -y install dnf-plugins-core
              dnf config-manager --set-enabled crb
              dnf -y install \
                https://dl.fedoraproject.org/pub/epel/epel-release-latest-10.noarch.rpm
              ;;
esac

# Clean the yum cache
yum clean all

packages=(gcc gcc-c++ make autoconf automake libtool \
          libtool-ltdl-devel openssl openssl-devel git \
          'perl(Test)' 'perl(Test::More)' 'perl(File::Spec)' \
          'perl(URI)' file sudo bison patch curl \
          pam pam-devel libedit libedit-devel)

# provides `cmp` used by `packaging/git-dirt-filter`
packages+=(diffutils)
if [[ $OS == *9 || $OS == *10 ]]; then

	# also install "zlib zlib-devel" because it's needed for `configure`ing
	# "gridftp/server/src"
	packages+=(zlib zlib-devel)
	# "perl-English" isn't installed by default, so install it explicitly,
	# because needed for "gridmap-tools-test.pl"
	packages+=(perl-English)
	# "perl-Sys-Hostname" isn't installed by default, so install it explicitly,
	# because needed for globus_ftp_client test scripts.
	packages+=(perl-Sys-Hostname)
fi

if [[ $TASK == tests ]]; then
    set +e
    [[ $COMPONENTS == *udt* ]]     && packages+=(udt-devel libnice-devel)
    [[ $COMPONENTS == *myproxy* ]] && packages+=(which)
    [[ $COMPONENTS == *gram* ]]    && packages+=('perl(Pod::Html)')
    set -e
elif [[ $TASK == *rpms ]]; then
    packages+=(rpm-build doxygen graphviz 'perl(Pod::Html)')
    # for globus-gridftp-server:
    packages+=(fakeroot)
    # for globus-xio-udt-driver:
    if [[ $OS == *9 ]]; then

        # libnice-devel is not available for CentOS Stream 9 / Rocky Linux 9.
        #
        # make_rpms.sh was also updated in this regard.
        :
    else
        packages+=(udt udt-devel glib2-devel libnice-devel gettext-devel libffi-devel)
    fi
    # for globus-gram-job-manager:
    packages+=(libxml2-devel)
    # for myproxy:
    packages+=(pam-devel voms-devel cyrus-sasl-devel openldap-devel voms-clients initscripts)
    # for globus-net-manager:
    packages+=(python3-devel)
    # for globus-gram-audit:
    packages+=('perl(DBI)')
    # for globus-scheduler-event-generator:
    if [[ $OS == *9 || $OS == *10 ]]; then

        # redhat-lsb-core is not available for CentOS Stream 9 / Rocky Linux 9.
        # But the default is also to not use LSB, so can be ignored.
        :
    else
        packages+=(redhat-lsb-core)
    fi
    # for gsi-openssh
    packages+=(pam libedit libedit-devel)
fi

# update potentially stale repo data before trying to install packages
dnf --refresh --allowerasing -y -d1 install "${packages[@]}"

# UID of travis user inside needs to match UID of travis user outside
getent passwd travis > /dev/null || useradd travis -u $TRAVISUID -o
# travis will require sudo when building RPMs to do yum installs
if [[ -d /etc/sudoers.d ]]; then
    echo "travis ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/10_travis
else
    echo -e "\ntravis ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers
fi

chown -R travis: /gct
cd /gct


print_file_size_table () {
    (
        set +x
        echo '==========================================================================================='
        echo "                                       $1"
        echo ' Name                                                                             Size'
        echo '-------------------------------------------------------------------------------- ----------'
        find "$2" -type f -printf '%-80f %10s\n' | sort
    )
}

make_tarballs() {
    su travis -c "/bin/bash -x /gct/travis-ci/make_source_tarballs.sh"
    print_file_size_table Tarballs /gct/package-output
}

make_srpms() {
    dist=$1
    su travis -c "/bin/bash -x /gct/travis-ci/make_rpms.sh -s -d $dist"
    print_file_size_table SRPMS /gct/packaging/rpmbuild/SRPMS
}

make_rpms() {
    nocheck=$1
    dist=$2
    su travis -c "/bin/bash -x /gct/travis-ci/make_rpms.sh $nocheck -d $dist"
    print_file_size_table SRPMS /gct/packaging/rpmbuild/SRPMS
    print_file_size_table RPMS /gct/packaging/rpmbuild/RPMS
}


case $TASK in
    tests)
        su travis -c "/bin/bash -x /gct/travis-ci/build_and_test.sh $OS $COMPONENTS"
        ;;
    tarballs)
        make_tarballs
        ;;
    srpms)
        make_tarballs
        echo '==========================================================================================='
        # NOTICE: No dashes in the dist string!
        make_srpms .gct.$release_ver

        # copy all the files we want to deploy into one directory b/c
        # can't specify multiple directories in travis
        mkdir -p travis_deploy

        cp -f packaging/rpmbuild/SRPMS/*.rpm package-output/*.tar.gz  \
              travis_deploy/
        ;;
    rpms)
        make_tarballs
        echo '==========================================================================================='
        # NOTICE: No dashes in the dist string!
        # -C = skip unit tests
        make_rpms -C .gct.$release_ver

        # copy all the files we want to deploy into one directory b/c
        # can't specify multiple directories in travis
        mkdir -p travis_deploy
        # HACK: only deploy the common files (tarballs, srpms) on one OS
        # to avoid attempting to overwrite the build products (which will
        # raise an error).
        # `overwrite: true` in .travis.yml ought to fix that, but doesn't
        # appear to.
        cp -f packaging/rpmbuild/SRPMS/*.rpm package-output/*.tar.gz  \
              travis_deploy/
        cp -f packaging/rpmbuild/RPMS/noarch/*.rpm packaging/rpmbuild/RPMS/x86_64/*.rpm  \
              travis_deploy/
        ;;
    *)
        echo "*** INVALID TASK '$TASK' ***"
        exit 2
        ;;
esac
