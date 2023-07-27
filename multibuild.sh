#!/bin/sh

# Convenience build script for w64devkit, primarily for automatic builds of
# multiple release flavors in one shot. By default it does a standard build
# at the current commit.
#
# Example, build a full release on an arbitrary commit:
#   $ ./multibuild.sh -a

set -e
archs=""
compact=no
dryrun=
flavors=""
suffix="$(git describe --exact-match 2>/dev/null | tr v - || true)"

usage() {
    cat <<EOF
usage: multibuild.sh [-48afhnOs] [-s SUFFIX]
  -4         Enable i686 build (default: no)
  -8         Enable x86_64 build (default: auto)
  -a         Enable all builds
  -f         Enable Fortran build (default: no)
  -h         Print this help message
  -n         Dry run, print commands but do nothing
  -O         Compact with advzip (default: no, less compatible)
  -s SUFFIX  Append a version suffix (e.g. "-s -1.2.3", default:auto)
EOF
}

while getopts 48abfhmnOs: opt; do
    case $opt in
        4) archs="$archs w32devkit";;
        8) archs="$archs w64devkit";;
        a) flavors="vanilla fortran"; archs="w64devkit w32devkit";;
        f) flavors="fortran";;
        h) usage; exit 0;;
        n) dryrun=echo;;
        O) compact=yes;;
        s) suffix="$OPTARG";;
        ?) usage >&2; exit 1;;
    esac
done
shift $((OPTIND - 1))

if [ $# -gt 0 ]; then
    printf 'multibuild.sh: Too many arguments\n' >&2
    usage >&2
    exit 1
fi

if [ -z "$archs" ]; then
    archs="w64devkit"
fi

target="tmp-w64-$$"
cleanup() {
    $dryrun git checkout .
    $dryrun git stash pop
    $dryrun docker rmi --no-prune $target || true
}
trap cleanup INT TERM

$dryrun git stash
zips=""
for arch in $archs; do
    for flavor in $flavors; do
        $dryrun git checkout .
        if [ $arch = w32devkit ]; then
            $dryrun patch -p1 -i src/variant-i686.patch
        fi
        suffix=""
        if [ $flavor = fortran ]; then
            suffix="-fortran"
            $dryrun patch -p1 -i src/variant-fortran.patch
        fi
        $dryrun docker build -t $target .
        if [ -n "$dryrun" ]; then
            $dryrun docker run --rm $target ">$arch$suffix.zip"
        else
            docker run --rm $target >$arch$suffix.zip
        fi
        zips="$zips $arch$suffix.zip"
    done
done

if [ $compact = yes ]; then
    printf "%s\n" $zips | xargs -I{} -P$(nproc) $dryrun advzip -z4 {}
fi

cleanup
