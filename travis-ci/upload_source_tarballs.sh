#!/bin/bash

# Upload source tarballs in the "package-output/" directory in the repo
# to a remote host, via SCP.

set -eux

repo_owner=$1

keyfile=$(pwd -P)/travis-ci/id_gctuploader

# repo.gridcf.org is an alias for this:
upload_server=hcc-osg-software2.unl.edu

# obtained by running "ssh-keyscan hcc-osg-software2.unl.edu"
hostsig="hcc-osg-software2.unl.edu ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQC2AIWAVx2KY+GhDab9SdxLTvjjzTiNa4pfHe7TvRZ5O+qZNc4c8sBlsG7OZGZvDLMRjGTKFyjJx3jDVUwaf14DwzQi9rgZxEZgBsRFffLATZqz+DyVN1H9uw215pah9Wh6yzaqMn51y6kqg0kk/ip62cYcXFgLKUNkzV0yz5WFugm5ziROZn01v5o74VdCABTAdlZhviUoObCn+bycXoUGGETY5GZ3muAW6y5LydDTD+2S97qJWGdSW7JBIfcmU7n5dl8MrtYKYwGswOgdUDrLtCp6CdZt/Evr+3NyLp35IhLnwxdkBBlKHPY0jXrGHyemsXa0Hq0PG/Ih5d0M8RMp"


echo "$hostsig" > ~/.ssh/known_hosts

(
    umask 077
    mkdir -p ~/.ssh
    openssl aes-256-cbc \
        -K $encrypted_677f6546cb93_key \
        -iv $encrypted_677f6546cb93_iv \
        -in "$keyfile.enc.$repo_owner" -out "$keyfile" \
        -d
)

root=$(git rev-parse --show-toplevel)
cd "$root"

cd package-output
rm -f gct-*.tar.gz
# ^ has a timestamp in the name so always gets updated whether anything changed
# or not. Between the git repo and the tarballs for the individual packages,
# this is unnecessary anyway.
sha512sum *.tar.gz > sha512sums

sftp \
    -o "PubkeyAuthentication=yes" \
    -o "IdentitiesOnly=yes" \
    -i "$keyfile" -b - gctuploader@$upload_server <<__END__
-mkdir gct6
cd gct6
-mkdir sources
cd sources
put *.tar.gz
put sha512sums
__END__
# vim:et:sts=4:sw=4
