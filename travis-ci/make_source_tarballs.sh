#!/bin/bash

# Create source tarballs for all components except gridftp-hdfs in a
# "package-output/" directory in the repo

# This can be run manually as well as part of a Travis-CI build

# rpm requirements:
#   make autoconf automake libtool libtool-ltdl-devel patch curl git bison openssl openssl-devel

set -eu

root=$(git rev-parse --show-toplevel)
cd "$root"

if [[ ! -d packaging ]]; then
    echo "$root/packaging directory missing -- are you in the right repo?"
    exit 1
fi

tmpfile=$(mktemp)
trap "rm -f $tmpfile" EXIT


if [[ ! -f Makefile ]]; then
    if [[ ! -f configure ]]; then
        echo '================================================================================'
        time autoreconf -i
    fi
    echo '================================================================================'
    time ./configure --enable-udt --enable-myproxy --enable-gram5-{server,lsf,sge,slurm,condor,pbs,auditing}
    # ^ not doing --enable-all because that would attempt to enable gridftp-hdfs
    #   which fails due to missing prerequisites
fi
rm -rf package-output
mkdir package-output
echo '================================================================================'
sed -i 's/gridftp_hdfs-dist//' Makefile

# Also create source installer tarball
package_name=$(grep '^PACKAGE_NAME =' Makefile | cut -d ' ' -f 3)
package_version=$(grep '^PACKAGE_VERSION =' Makefile | cut -d ' ' -f 3)
make dist && mv "${package_name}-${package_version}.tar.gz" package-output/

time make -j1 tarballs

echo '================================================================================'
pushd "$root/myproxy/oauth/source"
time python setup.py sdist
mv dist/*.tar.gz "$root/package-output/"
popd

