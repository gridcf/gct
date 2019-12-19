Installation instructions
=========================


Minimal tools needed
--------------------

The minimal RPMs (as provided by CentOS 7 yum) needed to build this package are:
`autoconf automake make libtool libtool-ltdl libtool-ltdl-devel gcc gcc-c++ patch openssl openssl-devel`

The minimal DEBs (as provided by Ubuntu 18.04) are
`autoconf automake make libtool libltdl-dev gcc g++ patch openssl libssl-dev pkg-config`

If not already installed, please also add

`curl`

Additionally, if UDT support is desired, you will need

`udt-devel` (CentOS) / `libudt-dev` (Ubuntu)

Note: For CentOS `udt-devel` comes from the EPEL repositories. Install 

`epel-release`

to get them. For Ubuntu the package is available from component "universe" which is enabled by default.


Install instructions:
--------------------

```
autoreconf -i
./configure <options>
make -j <numcpus>
make -j <numcpus> install
```

# for example:
# ./configure --disable-gram5 --disable-gsi-openssh --enable-udt
# make -j 4
# make -j 4 install

The files will be avaialble in
/usr/local/globus-6/*
(bin, lib, etc.)

