#!/bin/bash

id
env | sort

cd /gct
autoreconf -if
./configure
make

make check | tee check.out
