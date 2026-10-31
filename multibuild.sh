#!/bin/sh

# Convenience build script for w64devkit, primarily for automatic builds of
# multiple release flavors in one shot. By default it does a standard build
# at the current commit.
#
# Example, build a full release on an arbitrary commit:
#   $ ./multibuild.sh -as "$(git describe | tr v -)"

set -e
variants=
dryrun=
docker="${DOCKER:-docker}"
suffix="$(git describe --exact-match 2>/dev/null | tr v - || true)"

usage() {
    cat <<EOF
usage: multibuild.sh [-48ahn] [-s SUFFIX]
  -4         Enable x86 build
  -8         Enable x64 build
  -a         All variants
  -h         Print this help message
  -n         Dry run, print commands but do nothing
  -s SUFFIX  Append a version suffix (default: auto from git tag)

Environment:
  DOCKER     Container tool to use (default: docker)
EOF
}

while getopts 48ahns: opt; do
    case $opt in
        4) variants="$variants x86";;
        8) variants="$variants x64";;
        a) variants="x64 x86";;
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

: ${variants:=x64}
target="tmp-w64-$$"
cleanup() {
    $dryrun $docker rmi --no-prune $target 2>/dev/null || true
}
trap cleanup EXIT

for variant in $variants; do
    $dryrun $docker build --build-arg VARIANT=$variant -t $target .
    out="w64devkit-$variant$suffix.7z.exe"
    if [ -n "$dryrun" ]; then
        $dryrun $docker run --rm $target ">$out"
    else
        $docker run --rm $target >"$out"
    fi
done
