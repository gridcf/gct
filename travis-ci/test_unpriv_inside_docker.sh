#!/bin/bash

id
env | sort

cd /gct
autoreconf -if
./configure --prefix=/gct --enable-myproxy
make
make install

export PATH=/gct/bin:$PATH LD_LIBRARY_PATH=/gct/lib:$LD_LIBRARY_PATH

make check | tee check.out
