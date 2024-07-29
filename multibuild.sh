#!/bin/sh

# Convenience build script for w64devkit, primarily for automatic builds of
# multiple release flavors in one shot. By default it does a standard build
# at the current commit.
#
# Example, build a full release on an arbitrary commit:
#   $ ./multibuild.sh -as "$(git describe | tr v -)"

set -e
arch=""
dryrun=
flavors=""
suffix="$(git describe --exact-match 2>/dev/null | tr v - || true)"

usage() {
    cat <<EOF
usage: multibuild.sh [-48abfhnOs] [-s SUFFIX]
  -4         Enable x86 build (default: no)
  -8         Enable x64 build (default: auto)
  -a         All: Enable all builds
  -h         Print this help message
  -n         Dry run, print commands but do nothing
  -O         Compact with advzip (default: no, less compatible)
  -s SUFFIX  Append a version suffix (e.g. "-s -1.2.3", default:auto)
EOF
}

while getopts 48abfhmnOs: opt; do
    case $opt in
        4) arch="$arch w64devkit-x86";;
        8) arch="$arch w64devkit-x64";;
        a) flavors="X"; arch="w64devkit-x64 w64devkit-x86";;
        h) usage; exit 0;;
        n) dryrun=echo;;
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

if [ -z "$arch" ]; then
    arch="w64devkit-x64"
fi
if [ -z "$flavors" ]; then
    flavors="X"
fi

builds=
for base in $arch; do
    for flavor in $flavors; do
        builds="$builds $base$(echo $flavor | tr -d X)"
    done
done

target="tmp-w64-$$"
cleanup() {
    $dryrun git checkout .
    $dryrun git stash pop
    $dryrun docker rmi --no-prune $target || true
}
trap cleanup INT TERM

$dryrun git stash
for build in $builds; do
    $dryrun git checkout .
    (
        IFS=-
        set $build; shift
        for flavor in "$@"; do
            if [ -e src/variant-$flavor.patch ]; then
                $dryrun patch -p1 -i src/variant-$flavor.patch
            fi
        done
    )
    $dryrun docker build -t $target .
    if [ -n "$dryrun" ]; then
        $dryrun docker run --rm $target ">$build$suffix.exe"
    else
        docker run --rm $target >$build$suffix.exe
    fi
done

cleanup
