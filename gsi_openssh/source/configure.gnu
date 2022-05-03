#! /bin/sh
"${0%.gnu}" "$@" --without-zlib-version-check --with-ssl-engine --with-ipaddr-display --with-pam --without-kerberos5 --with-libedit --with-gsi --sysconfdir="\${prefix}/etc/gsissh"