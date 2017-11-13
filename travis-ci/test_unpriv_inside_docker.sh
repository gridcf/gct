#!/bin/bash

COMPONENTS=$1

id
env | sort

cd /gct
autoreconf -if
args=(--prefix=/gct --enable-silent-rules)
if [[ $COMPONENTS == *myproxy* ]]; then
    args+=(--enable-myproxy)
fi
if [[ $COMPONENTS == *udt* ]]; then
    args+=(--enable-udt)
fi
if [[ $COMPONENTS == *gram5* ]]; then
    args+=(--enable-gram5-{server,lsf,sge,slurm,condor,pbs,auditing})
else
    args+=(--disable-gram5)
fi
./configure "${args[@]}"
make
make install

export PATH=/gct/bin:$PATH LD_LIBRARY_PATH=/gct/lib:$LD_LIBRARY_PATH

make check | tee check.out
