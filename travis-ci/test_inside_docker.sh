#!/bin/bash

IMAGE=$1
COMPONENTS=$2


set -xe

# Clean the yum cache
yum clean all

packages=(gcc gcc-c++ make autoconf automake libtool patch \
          libtool-ltdl-devel openssl openssl-devel git \
          'perl(Test::More)' 'perl(File::Spec)' 'perl(URI)' \
          file)

set +e
[[ $COMPONENTS == *ssh* ]]     && packages+=(curl)
[[ $COMPONENTS == *udt* ]]     && packages+=(glib2 xz)
[[ $COMPONENTS == *myproxy* ]] && packages+=(which)
[[ $COMPONENTS == *gram* ]]    && packages+=('perl(Pod::Html)')
set -e

yum -y -d1 install "${packages[@]}"

getent passwd builduser > /dev/null || useradd builduser
chown -R builduser: /gct
su builduser -c "/bin/bash -xe /gct/travis-ci/test_unpriv_inside_docker.sh $IMAGE $COMPONENTS"

set +x
echo "**** TESTS COMPLETED *****"
cd /gct
grep '^FAIL:\|^PASS:\|^SKIP:' check.out
count=`grep -c '^FAIL:' check.out` || :
if [[ $count -ge 1 ]]; then
    echo "**** $count TESTS FAILED ****"
    find . -wholename \*/test/test-suite.log | while read logfile; do
        echo "=== $logfile ==="
        cat "$logfile"
        echo
    done
    exit 1
fi
