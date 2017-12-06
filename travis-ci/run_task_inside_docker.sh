#!/bin/bash

IMAGE=$1
TASK=$2
COMPONENTS=$3


set -xe

# Clean the yum cache
yum clean all

yum install -y -d0 patch

packages=(gcc gcc-c++ make autoconf automake libtool \
          libtool-ltdl-devel openssl openssl-devel git \
          'perl(Test)' 'perl(Test::More)' 'perl(File::Spec)' \
          'perl(URI)' file sudo bison)

set +e
[[ $COMPONENTS == *ssh* ]]     && packages+=(curl)
[[ $COMPONENTS == *udt* ]]     && packages+=(glib2 xz)
[[ $COMPONENTS == *myproxy* ]] && packages+=(which)
[[ $COMPONENTS == *gram* ]]    && packages+=('perl(Pod::Html)')
set -e

if [[ $TASK == rpms ]]; then
    (cd /gct; patch -p1 < /gct/travis-ci/Disable-globus-gram-job-manager-RPM-check.patch)
    nocheck=-C
    case $IMAGE in
        *:centos6)  yum -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-6.noarch.rpm
                    nocheck=
                    # ^ centos 6 doesn't support rpmbuild --nocheck
                    ;;
        *:centos7)  yum -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
                    ;;
    esac
    packages+=(rpm-build doxygen graphviz 'perl(Pod::Html)')
    # for globus-gridftp-server:
    packages+=(fakeroot)
    # for globus-xio-udt-driver:
    packages+=(udt udt-devel glib2-devel libnice-devel gettext-devel libffi-devel)
    # for globus-gram-job-manager:
    packages+=(libxml2-devel)
    # for myproxy:
    packages+=(pam-devel voms-devel cyrus-sasl-devel openldap-devel voms-clients initscripts)
    # for globus-net-manager:
    packages+=(python-devel)
    # for globus-gram-audit:
    packages+=('perl(DBI)')
    # for globus-scheduler-event-generator:
    packages+=(redhat-lsb-core)
    # for myproxy-oauth
    packages+=(m2crypto mod_ssl mod_wsgi pyOpenSSL python-crypto)
fi


yum -y -d1 install "${packages[@]}"

getent passwd builduser > /dev/null || useradd builduser
# builduser will require sudo when building RPMs to do yum installs
if [[ -d /etc/sudoers.d ]]; then
    echo "builduser ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/10_builduser
else
    echo -e "\nbuilduser ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers
fi
chown -R builduser: /gct


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
    
case $TASK in
    (tests)
        su builduser -c "/bin/bash -xe /gct/travis-ci/build_and_test.sh $IMAGE $COMPONENTS"
        ;;
    (tarballs)
        cd /gct
        su builduser -c "/bin/bash -xe /gct/travis-ci/make_source_tarballs.sh"
        print_file_size_table Tarballs /gct/package-output
        ;;
    (rpms)
        cd /gct
        su builduser -c "/bin/bash -xe /gct/travis-ci/make_source_tarballs.sh"
        echo '==========================================================================================='
        # -C = skip unit tests
        su builduser -c "/bin/bash -xe /gct/travis-ci/make_rpms.sh $nocheck"
        print_file_size_table SRPMS /gct/packaging/rpmbuild/SRPMS
        print_file_size_table RPMS /gct/packaging/rpmbuild/RPMS
        ;;
    (*)
        echo "*** INVALID TASK '$TASK' ***"
        exit 2
        ;;
esac
