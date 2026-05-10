#!/bin/sh
# Build Go from source on Windows using w64devkit.
# Bootstraps from Go 1.4 through intermediate versions up to the latest.
#
# Usage: ./bootstrap-go-toolchain.sh [INSTALL_DIR]
#   INSTALL_DIR  Where to install the final Go toolchain (default: ./go)
#
# Prerequisites: w64devkit in PATH (provides gcc, wget, tar, patch, sh)
#
# The final version is patched with go-bigobj.patch located next to
# this script before building.
# Tarballs are verified against the SHA-256 values embedded below.
#
# Bootstrap chain (per https://go.dev/doc/install/source):
#   Go 1.4  -> built with C compiler (gcc from w64devkit)
#   Go 1.17 -> built with Go 1.4
#   Go 1.20 -> built with Go 1.17
#   Go 1.22 -> built with Go 1.20
#   Go 1.24 -> built with Go 1.22
#   Go 1.26 -> built with Go 1.24
#
# Go 1.N requires Go 1.M to build, where M = N-2 rounded down to even.

set -e

# The Windows bootstrap breaks if GOBIN is present in the environment.
unset GOBIN

# --- Version configuration (update as needed) ---
BOOTSTRAP_VERSIONS="1.17.13 1.20.14 1.22.12 1.24.6"
FINAL_VERSION=1.26.3

# --- Paths ---
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD="$SCRIPT_DIR/build"
DOWNLOADS="$SCRIPT_DIR/downloads"
PATCH_FILE="$SCRIPT_DIR/go-bigobj.patch"
INSTALL="${1:-$SCRIPT_DIR/go}"

# --- URLs ---
GO14_URL="https://github.com/golang/go/archive/refs/heads/release-branch.go1.4.tar.gz"
GO_DL_URL="https://go.dev/dl"

# --- SHA-256 sums ---
SHA256SUMS='10273543295cf4d97df5b1ff9b1db4089099b7cada99f14cfa1dfae9be789556  go1.4-branch.tar.gz
a1a48b23afb206f95e7bbaa9b898d965f90826f6f1d1fc0c1d784ada0cd300fd  go1.17.13.src.tar.gz
1aef321a0e3e38b7e91d2d7eb64040666cabdcc77d383de3c9522d0d69b67f4e  go1.20.14.src.tar.gz
012a7e1f37f362c0918c1dfa3334458ac2da1628c4b9cf4d9ca02db986e17d71  go1.22.12.src.tar.gz
e1cb5582aab588668bc04c07de18688070f6b8c9b2aaf361f821e19bd47cfdbd  go1.24.6.src.tar.gz
1c646875d0aa8799133184ed57cf79ff24bdefe8c8820470602a9d3d6d9192b8  go1.26.3.src.tar.gz'

lookup_checksum_entry() {
    name="$1"
    printf '%s\n' "$SHA256SUMS" | awk -v name="$name" '$2 == name { print; found=1; exit } END { if (!found) exit 1 }'
}

verify_download() {
    name="$1"
    entry="$(lookup_checksum_entry "$name")" || {
        printf 'Missing embedded checksum for %s\n' "$name" >&2
        return 1
    }

    (
        cd "$DOWNLOADS"
        printf '%s\n' "$entry" | sha256sum -c
    )
}

download() {
    dest="$1"
    url="$2"
    name="$(basename "$dest")"

    if [ -f "$dest" ]; then
        if verify_download "$name" >/dev/null; then
            printf 'Verified cached %s\n' "$name"
            return
        fi

        printf 'Checksum mismatch for %s, re-downloading\n' "$name"
        rm -f "$dest"
    fi

    printf "Downloading %s\n" "$name"
    wget -O "$dest" "$url"
    verify_download "$name"
}

echo "Go bootstrap build"
echo "  Install to: $INSTALL"
echo ""

if [ ! -f "$PATCH_FILE" ]; then
    printf "Missing required patch: %s\n" "$PATCH_FILE" >&2
    exit 1
fi

mkdir -p "$BUILD" "$DOWNLOADS"

# Download all source tarballs
download "$DOWNLOADS/go1.4-branch.tar.gz" "$GO14_URL"
for v in $BOOTSTRAP_VERSIONS $FINAL_VERSION; do
    download "$DOWNLOADS/go${v}.src.tar.gz" "$GO_DL_URL/go${v}.src.tar.gz"
done

# Build Go 1.4 from C source
echo ""
echo "===== Building Go 1.4 (C bootstrap) ====="
rm -rf "$BUILD/go1.4"
mkdir -p "$BUILD/go1.4"
tar xf "$DOWNLOADS/go1.4-branch.tar.gz" -C "$BUILD/go1.4" --strip-components=1
cd "$BUILD/go1.4/src"
CGO_ENABLED=0 cmd /c make.bat

PREV="$BUILD/go1.4"

# Build each intermediate version
for v in $BOOTSTRAP_VERSIONS; do
    echo ""
    echo "===== Building Go $v ====="
    rm -rf "$BUILD/go${v}"
    mkdir -p "$BUILD/go${v}"
    tar xf "$DOWNLOADS/go${v}.src.tar.gz" -C "$BUILD/go${v}" --strip-components=1
    cd "$BUILD/go${v}/src"
    CGO_ENABLED=0 GOROOT_BOOTSTRAP="$PREV" cmd /c make.bat
    PREV="$BUILD/go${v}"
done

# Build final version with patches
echo ""
echo "===== Building Go $FINAL_VERSION ====="
rm -rf "$BUILD/go${FINAL_VERSION}"
mkdir -p "$BUILD/go${FINAL_VERSION}"
tar xf "$DOWNLOADS/go${FINAL_VERSION}.src.tar.gz" -C "$BUILD/go${FINAL_VERSION}" --strip-components=1

printf "Applying %s\n" "$(basename "$PATCH_FILE")"
(cd "$BUILD/go${FINAL_VERSION}" && patch -p1 < "$PATCH_FILE")

cd "$BUILD/go${FINAL_VERSION}/src"
CGO_ENABLED=1 GOROOT_BOOTSTRAP="$PREV" cmd /c make.bat

# Install
echo ""
echo "===== Installing ====="
rm -rf "$INSTALL"
cp -r "$BUILD/go${FINAL_VERSION}" "$INSTALL"

echo ""
echo "Go $FINAL_VERSION installed to $INSTALL"
echo "Add $INSTALL/bin to your PATH."
