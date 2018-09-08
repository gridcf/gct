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
cat > ~/.ssh/config <<__END__
Host $upload_server
User gctuploader
IdentityFile $keyfile
PubkeyAuthentication yes
IdentitiesOnly yes
__END__

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

sftp -b - $upload_server &>/dev/null <<__END__
-mkdir gct6
cd gct6
-mkdir sources
__END__


remote_tarball_ok () {
    local tarball remote_tarball
    tarball=$1
    remote_tarball=$2

    local tmp=$(mktemp -d)
    trap "rm -rf $tmp" RETURN
    # Check for file and sha512sum existence. Download the file so we
    # can run extra tests.
    sftp -b - $upload_server <<__END__
get $remote_tarball $tmp/$tarball
get $remote_tarball.sha512 $tmp/$tarball.sha512
__END__
    if [[ $? -ne 0 ]]; then
        echo "***** Couldn't download remote tarball $tarball and/or checksum ******"
        return 1
    fi

    # Check that the downloaded sha512sum matches the downloaded file.
    pushd $tmp
    sha512sum --quiet -c $tarball.sha512
    local ret=$?
    popd
    if [[ $ret -ne 0 ]]; then
        echo "****** Remote tarball $tarball checksum doesn't match ******"
        return 1
    fi

    # Check that the downloaded file is a tarball.
    filetype=$(file -bzi "$tmp/$tarball")
    if [[ $filetype != *application/x-tar* ]]; then
        echo "****** Remote tarball $tarball doesn't look like a tarball ******"
        return 1
    fi

    return 0
}


# Create individual checksum files instead of one big one because we want to
# keep checksums for old tarballs that are already in the repo.
set +e
for tarball in *.tar.gz; do
    remote_tarball=gct6/sources/$tarball
    # Don't upload the tarball if it already exists and is valid
    if remote_tarball_ok "$tarball" "$remote_tarball"; then
        echo "****** Remote tarball $tarball OK... leaving remote as-is ******"
    else
        echo "****** Checksumming and uploading $tarball ******"
        sha512sum "$tarball" > "$tarball.sha512"
        sftp -b - $upload_server <<__END__
put "$tarball" "$remote_tarball"
put "$tarball.sha512" "$remote_tarball.sha512"
__END__
    fi
done
set -e

# vim:et:sts=4:sw=4
