#!/bin/sh
# Run-time entrypoint for the `signed` Docker target. Signs every PE
# inside $PREFIX, packs the toolchain with 7z, signs the resulting
# self-extracting archive, and streams it on stdout.
#
# aas-sign reads AZURE_CLIENT_ID and AZURE_TENANT_ID from the env, and
# fetches an Azure access token via GitHub OIDC using the runner-injected
# ACTIONS_ID_TOKEN_REQUEST_URL / ACTIONS_ID_TOKEN_REQUEST_TOKEN. All four
# must be forwarded with `docker run -e`. The signing target (endpoint /
# account / profile) is taken from environment variables below; they're
# not secret but they're configuration the container shouldn't bake in.
#
# aas-sign 1.1.0+ retries transient TLS / network errors internally,
# so this script just calls it once and trusts it.

set -e
: "${AAS_SIGN_ENDPOINT:?AAS_SIGN_ENDPOINT not set}"
: "${AAS_SIGN_ACCOUNT:?AAS_SIGN_ACCOUNT not set}"
: "${AAS_SIGN_PROFILE:?AAS_SIGN_PROFILE not set}"

# Reserve fd 3 for the signed package; everything else goes to stderr so
# that progress output doesn't corrupt the binary stream on stdout.
exec 3>&1 1>&2

# Sign every executable and DLL inside the toolchain. Toolchain paths
# don't contain whitespace, so plain word splitting through xargs is
# fine here.
find "$PREFIX" -type f \( -name '*.exe' -o -name '*.dll' \) -print0 \
  | xargs -0 aas-sign sign --max-parallel 8 \
                           --endpoint "$AAS_SIGN_ENDPOINT" \
                           --account  "$AAS_SIGN_ACCOUNT" \
                           --profile  "$AAS_SIGN_PROFILE"

# Pack the (now-signed) toolchain.
7z a -mx=9 -mtm=- w64devkit.7z "$PREFIX"

# Concatenate the SFX prefix with the archive, then sign the result.
cat /7z/7z.sfx w64devkit.7z >/w64devkit.7z.exe
aas-sign sign --endpoint "$AAS_SIGN_ENDPOINT" \
              --account  "$AAS_SIGN_ACCOUNT" \
              --profile  "$AAS_SIGN_PROFILE" \
              /w64devkit.7z.exe

exec 1>&3 3>&-
exec cat /w64devkit.7z.exe
