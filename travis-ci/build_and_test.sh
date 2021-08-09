#!/bin/bash

set -e

OS=$1
COMPONENTS=$2

root=$(git rev-parse --show-toplevel)
cd "$root"

args=(--prefix="$root" --enable-silent-rules)
if [[ $COMPONENTS != *ssh* ]]; then
    args+=(--disable-gsi-openssh)
fi
if [[ $COMPONENTS == *gram5* ]]; then
    args+=(--enable-gram5-{server,lsf,sge,slurm,condor,pbs,auditing})
else
    args+=(--disable-gram5)
fi
set +e
[[ $COMPONENTS == *myproxy* ]] && args+=(--enable-myproxy)
[[ $COMPONENTS == *udt* ]]     && args+=(--enable-udt)
[[ $OS         == fedora* ]]   && args+=(LIBS='-ldl')
set -e

echo '================================================================================'
time autoreconf -i
echo '================================================================================'
time ./configure "${args[@]}"
echo '================================================================================'
time make -j
echo '================================================================================'
time make -j install
export PATH="$root"/bin:$PATH LD_LIBRARY_PATH="$root"/lib:$LD_LIBRARY_PATH
echo '================================================================================'
time make -j check | tee check.out
echo '================================================================================'

set +x
echo '===== TESTS COMPLETED =========================================================='
grep '^FAIL:\|^PASS:\|^SKIP:\|^XFAIL:\|^XPASS:\|^ERROR:' check.out  ||  \
    {
        echo "Failure: no apparent test output.  Full log:"
        cat check.out
        exit 1
    }
count=`grep -c '^FAIL:\|^ERROR:' check.out` || :
if [[ $count -ge 1 ]]; then
    echo "**** $count TESTS FAILED ****"
    find . -wholename \*/test/test-suite.log | while read logfile; do
        echo "=== $logfile ==="
        cat "$logfile"
        echo
    done
    exit 1
fi
