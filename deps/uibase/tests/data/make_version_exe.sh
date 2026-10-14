#!/usr/bin/env bash
# Regenerates version.exe, the tiny PE executable used by test_linux_utility.cpp
# (FileVersion 1.2.3.4 in VS_FIXEDFILEINFO, ProductVersion string "5.6.7-fixture").
# The binary is committed; this script documents how it was made.
#
# Needs the MinGW-w64 cross toolchain (Debian/Ubuntu: apt-get install mingw-w64,
# Fedora: dnf install mingw64-gcc).
set -euo pipefail

# the linker and strip write the build time into the PE header unless this is set
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-0}"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
prefix="${MINGW_PREFIX:-x86_64-w64-mingw32}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$prefix-windres" -O coff -i "$here/version.rc" -o "$tmp/version.res"
"$prefix-gcc" -Os -s -nostdlib -Wl,--entry=start \
    -o "$here/version.exe" "$here/version.c" "$tmp/version.res"
"$prefix-strip" --strip-all "$here/version.exe"
chmod -x "$here/version.exe"
echo "wrote $here/version.exe ($(stat -c %s "$here/version.exe") bytes)"
