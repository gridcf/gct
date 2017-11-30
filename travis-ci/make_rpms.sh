#!/bin/bash

set -e
usage()
{
    echo "Usage: $0 [<options>]"
    echo
    echo "Options:"
    echo "  -h                              This message"
    echo "  -C                              Skip %check when building RPMs"
}


umask 022


rootdir=$(git rev-parse --show-toplevel)
packagingdir=$rootdir/packaging

cd "$packagingdir"

fedoradir=$packagingdir/fedora
tarballdir=$rootdir/package-output
topdir=$packagingdir/rpmbuild
globusversion=
nocheck=

if ! ls "$tarballdir"/*.tar.gz >/dev/null; then
    echo >&2 "No tarballs found in package-output directory"
    echo >&2 "Run make_source_tarballs.sh to generate tarballs"
    exit 1
fi


while getopts hC i; do
    case "$i" in
        h)
            usage
            exit 0
            ;;
        C)
            nocheck=1
            ;;
    esac
done

shift $(($OPTIND - 1))

rm -rf "$topdir"
mkdir -p "$topdir"/{BUILD,BUILDROOT,SOURCES,RPMS,SPECS,SRPMS,tmp,log}
# RPM expects .rpmmacros under $HOME so use our temp dir for it
HOME=$topdir

targets=(x86_64-linux)
# targets+=(i686-linux)

remove_installed_gct_rpms()
{
    rpm -qa | egrep '^globus-|^myproxy' \
        | xargs --no-run-if-empty \
            sudo rpm -e $pkgs_to_rm
}

# Enable building of RPM packages in $topdir
cat <<EOF >> "$topdir/.rpmmacros"
%_topdir               $topdir
%_tmppath              $topdir/tmp
# Override this, as it breaks documentation dependencies in some of the builds
%_excludedocs 0
EOF


packages=( $(grep -v '^#' $fedoradir/ORDERING | grep '[^[:space:]]') )

cp -f "$tarballdir"/*.tar.gz "$topdir"/SOURCES

for rpmname in ${packages[*]}; do
    if [[ ! -f $fedoradir/$rpmname.spec ]]; then
        echo >&2 "Spec file not found for $rpmname"
        exit 1
    fi

    rpmbuild -bs --nodeps "$fedoradir/$rpmname.spec";
done


build_all_for_target()
{
    target=$1

    # Remove everything prior to building
    remove_installed_gct_rpms

    for rpmname in ${packages[*]}; do
        echo "*** Building $rpmname ***"
        rpmpath=$(echo $topdir/SRPMS/$rpmname-[0-9]*.src.rpm)
        if [[ ! -f $rpmpath ]]; then
            echo >&2 "SRPM not found for $rpmname"
            return 1
        fi

        logpath=$topdir/log/$rpmname.log
        rpmbuild ${nocheck:+--nocheck} --target $target \
                 --rebuild "$rpmpath" > "$logpath" 2>&1 || {
            echo >&2 "Error! Logs follow:"
            cat >&2 "$logpath"
            return 1
        }
        rpmsdir=$topdir/RPMS

        _rpms=( $(grep "^Wrote: " "$logpath" | cut -c 7-) )
        if [[ ${#_rpms[@]} -lt 1 ]]; then
            echo >&2 "Error! No RPMs written! Logs follow:"
            cat >&2 "$logpath"
            return 1
        fi
        # HACK: exclude globus-gram-job-manager-*-setup-seg (conflicts with globus-gram-job-manager-*-setup-poll)
        # Using *-setup-poll because globus-gram-job-manager explicitly BuildRequires it
        rpms=()
        for r in ${_rpms[@]}; do
            [[ $r == */globus-gram-job-manager-*-setup-seg-* ]] || rpms+=($r)
        done
        sudo rpm -hUv "${rpms[@]}"
    done
}  # build_all_for_target()

for target in ${targets[@]}; do
    build_all_for_target $target
done

# Remove what we built
remove_installed_gct_rpms

