#!/bin/sh

# Convenience build script for w64devkit, primarily for automatic builds of
# multiple release flavors in one shot. By default it does a standard build
# at the current commit.
#
# Example, build a full release on an arbitrary commit:
#   $ ./multibuild.sh -as "$(git describe | tr v -)"

set -e
arch=""
compact=no
dryrun=
flavors=""
suffix="$(git describe --exact-match 2>/dev/null | tr v - || true)"

usage() {
    cat <<EOF
usage: multibuild.sh [-48abfhnOs] [-s SUFFIX]
  -4         Enable i686 build (default: no)
  -8         Enable x86_64 build (default: auto)
  -a         All: Enable all builds
  -h         Print this help message
  -n         Dry run, print commands but do nothing
  -O         Compact with advzip (default: no, less compatible)
  -s SUFFIX  Append a version suffix (e.g. "-s -1.2.3", default:auto)
EOF
}

while getopts 48abfhmnOs: opt; do
    case $opt in
        4) arch="$arch w64devkit-i686";;
        8) arch="$arch w64devkit";;
        a) flavors="X"; arch="w64devkit w64devkit-i686";;
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

if [ -z "$arch" ]; then
    arch="w64devkit"
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
            $dryrun patch -p1 -i src/variant-$flavor.patch
        done
    )
    $dryrun docker build -t $target .
    if [ -n "$dryrun" ]; then
        $dryrun docker run --rm $target ">$build$suffix.zip"
    else
        docker run --rm $target >$build$suffix.zip
    fi
done

if [ $compact = yes ]; then
    printf "%s$suffix.zip\n" $builds \
        | xargs -I{} -P$(nproc) $dryrun advzip -z4 {}
fi

cleanup
