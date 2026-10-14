#!/usr/bin/env bash
# Regenerates the (tiny) archives used by test_archive.cpp. They are committed, this
# script only documents how they were made and can be re-run to rebuild them.
#
#   SEVENZIP=/path/to/7zz ./make_fixtures.sh
#
# SEVENZIP is the 7-Zip console (7zz) 26.01. It is not installed by build.sh; build it
# from the tree that build/7zip/build-7zip.sh caches in build-out/7zip/src with:
#
#   cd build-out/7zip/src/CPP/7zip/Bundles/Alone2
#   make -f ../../cmpl_gcc.mak O=/tmp/7zz-obj CFLAGS_WARN_WALL="-Wall -Wextra"
#   -> /tmp/7zz-obj/7zz
#
# python3 (standard library only) is used for the ZIP files. Content:
#   simple.7z, simple.zip   a.txt = "hello", dir/B.txt = "world"
#   backslash.zip           entries "a.txt" and "dir\B.txt" (as written by some
#                           Windows tools), must extract to dir/B.txt
#   unicode.7z              Ünïcödé/日本.txt = "unicode"
#   password.7z             a.txt, dir/B.txt, password "mo2", encrypted headers
#   corrupt.7z              simple.7z truncated to its first half
#
# 7-Zip uses a random salt for password.7z, so regenerating it changes its bytes (the
# other files are reproducible).
#
# There is no RAR fixture: no free tool can create .rar files (see test_archive.cpp).
set -euo pipefail

SEVENZIP="${SEVENZIP:?set SEVENZIP to the 7zz binary}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# fixed timestamps keep the output reproducible
STAMP="2020-01-01 00:00:00"
mkdir -p "$WORK/simple/dir" "$WORK/unicode/Ünïcödé"
printf 'hello' > "$WORK/simple/a.txt"
printf 'world' > "$WORK/simple/dir/B.txt"
printf 'unicode' > "$WORK/unicode/Ünïcödé/日本.txt"
find "$WORK" -exec touch -d "$STAMP" {} +

sz() {  # sz <archive> <files...> [extra switches]; run from the content directory
    local out="$1"; shift
    rm -f "$out"
    "$SEVENZIP" a -bso0 -bsp0 -mtm=on -mtc=off -mta=off "$out" "$@"
}

(cd "$WORK/simple" && sz "$HERE/simple.7z" a.txt dir)
(cd "$WORK/simple" && sz "$HERE/password.7z" a.txt dir -pmo2 -mhe=on)
(cd "$WORK/unicode" && sz "$HERE/unicode.7z" Ünïcödé)

size=$(stat -c %s "$HERE/simple.7z")
head -c $((size / 2)) "$HERE/simple.7z" > "$HERE/corrupt.7z"

python3 -I - "$HERE" <<'PY'
import sys, zipfile
here = sys.argv[1]
def write(name, entries):
    with zipfile.ZipFile(f"{here}/{name}", "w", zipfile.ZIP_DEFLATED) as z:
        for arcname, data in entries:
            info = zipfile.ZipInfo(arcname, date_time=(2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)
write("simple.zip", [("a.txt", b"hello"), ("dir/B.txt", b"world")])
write("backslash.zip", [("a.txt", b"hello"), ("dir\\B.txt", b"world")])
PY
