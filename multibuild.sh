#!/bin/sh

# Convenience build script for w64devkit, primarily for automatic builds of
# multiple release flavors in one shot. By default it does a standard build
# at the current commit.
#
# Example, build a full release on an arbitrary commit:
#   $ ./multibuild.sh -as "$(git describe | tr v -)"

set -e
arch=""
compact=auto
dryrun=
flavors=""
suffix="$(git describe --exact-match 2>/dev/null | tr v - || true)"

usage() {
    cat <<EOF
usage: multibuild.sh [-48abfhmnq] [-s SUFFIX]
  -4         Enable i686 build (default: no)
  -8         Enable x86_64 build (default: auto)
  -a         All: Enable all builds
  -b         Enable vanilla build (default: auto)
  -f         Enable Fortran build (default: no)
  -h         Print this help message
  -m         Enable mini build (default: no)
  -n         Dry run, print commands but do nothing
  -s SUFFIX  Append a version suffix (e.g. "-s -1.2.3", default:auto)
  -q         Quick: do no compact with advzip (default: auto)
EOF
}

while getopts 48abfhmnqs: opt; do
    case $opt in
        4) arch="$arch w64devkit-i686";;
        8) arch="$arch w64devkit";;
        a) flavors="X -mini -fortran"; arch="w64devkit w64devkit-i686";;
        b) flavors="$flavors X";;
        f) flavors="$flavors -fortran";;
        h) usage; exit 0;;
        m) flavors="$flavors -mini";;
        n) dryrun=echo;;
        s) suffix="$OPTARG";;
        q) compact=no;;
        ?) usage >&2; exit 1;;
    esac
done
shift $((OPTIND - 1))

if [ $# -gt 0 ]; then
    printf 'multibuild.sh: Too many arguments\n' >&2
    usage >&2
    exit 1
fi

if [ $compact = auto ] && command -v advzip >/dev/null 2>&1; then
    compact=yes
else
    compact=no
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
        | xargs -I{} -P$(nproc) $dryrun advzip -z3 {}
fi

cleanup
