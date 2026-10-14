#!/usr/bin/env bash
# Build 7-Zip 26.01 as the shared library 7z.so (CPP/7zip/Bundles/Format7zF) and
# install it with its headers and a CMake package into <root>/install.
#
#   build/7zip/build-7zip.sh
#
# Environment (all optional):
#   INSTALL    install prefix            (default <root>/install)
#   BUILD_OUT  build tree root           (default <root>/build-out)
#   CC, CXX    compilers; the default is the system gcc/g++
#
# Produces:
#   $INSTALL/lib/7z.so
#   $INSTALL/include/7zip/{C,CPP}/**.h
#   $INSTALL/share/7zip/src/MyWindows.cpp   (see below)
#   $INSTALL/lib/cmake/7zip/7zip-config.cmake   (find_package(7zip CONFIG))
#   $INSTALL/share/licenses/7zip/License.txt
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
INSTALL="${INSTALL:-$ROOT/install}"
BUILD_OUT="${BUILD_OUT:-$ROOT/build-out}"

SEVENZIP_REPO="https://github.com/ip7z/7zip"
SEVENZIP_TAG="26.01"
# Commit the tag must resolve to (the tag is moveable, the commit is not).
SEVENZIP_COMMIT="8c63d71ff886bda90c86db28466287f977374237"

WORK="$BUILD_OUT/7zip"
SRC="$WORK/src"
OBJ="$WORK/obj"

log() { echo "==> 7zip: $*"; }
die() { echo "build-7zip.sh: $*" >&2; exit 1; }

# --- fetch (cached in build-out/7zip/src, verified by commit hash) ----------
head_of() { git -C "$SRC" rev-parse HEAD 2>/dev/null || true; }

if [[ $(head_of) != "$SEVENZIP_COMMIT" ]]; then
    log "fetching $SEVENZIP_REPO @ $SEVENZIP_TAG"
    rm -rf "$SRC"
    mkdir -p "$WORK"
    git clone --quiet --depth 1 --branch "$SEVENZIP_TAG" -c advice.detachedHead=false \
        "$SEVENZIP_REPO" "$SRC"
    [[ $(head_of) == "$SEVENZIP_COMMIT" ]] ||
        die "tag $SEVENZIP_TAG resolved to $(head_of), expected $SEVENZIP_COMMIT"
fi
log "source at $SEVENZIP_COMMIT"

# --- build 7z.so ------------------------------------------------------------
# cmpl_gcc.mak = makefile.gcc with the plain (no assembler) GCC settings.
# Upstream builds with -Werror, which newer GCC releases trip over (-Warray-bounds
# in HandlerCont.cpp with GCC 16), so keep the warnings but do not fail on them.
BUNDLE="$SRC/CPP/7zip/Bundles/Format7zF"
mkdir -p "$OBJ"
log "building 7z.so"
(
    cd "$BUNDLE"
    make -s -f ../../cmpl_gcc.mak -j"$(nproc)" O="$OBJ" \
        CFLAGS_WARN_WALL="-Wall -Wextra" \
        CC="${CC:-gcc}" CXX="${CXX:-g++}"
)
[[ -f $OBJ/7z.so ]] || die "build did not produce $OBJ/7z.so"

# --- install ----------------------------------------------------------------
log "installing into $INSTALL"
install -D -m 0755 "$OBJ/7z.so" "$INSTALL/lib/7z.so"

# Header tree: consumers use "7zip/Archive/IArchive.h" (include dir .../7zip/CPP),
# whose own includes reach ../../C/* and ../../Common/*, so keep C/ and CPP/ intact.
INC="$INSTALL/include/7zip"
rm -rf "$INC"
mkdir -p "$INC"
(cd "$SRC" && find C CPP -name '*.h' -print0 | tar --null -T - -cf -) | tar -C "$INC" -xf -

# CPP/Common/MyWindows.cpp implements, on non-Windows systems, the Win32/COM functions
# that 7-Zip's headers declare (SysAllocString, VariantClear, GetLastError, ...). It is
# not exported by 7z.so, so a consumer that talks to 7z.so through its COM interfaces
# (deps/archive) compiles this one file itself; the package exposes it as
# 7zip_MYWINDOWS_SOURCE.
install -D -m 0644 "$SRC/CPP/Common/MyWindows.cpp" "$INSTALL/share/7zip/src/MyWindows.cpp"

install -D -m 0644 "$HERE/7zip-config.cmake" "$INSTALL/lib/cmake/7zip/7zip-config.cmake"
install -D -m 0644 "$HERE/7zip-config-version.cmake" "$INSTALL/lib/cmake/7zip/7zip-config-version.cmake"
install -D -m 0644 "$SRC/DOC/License.txt" "$INSTALL/share/licenses/7zip/License.txt"
install -D -m 0644 "$SRC/DOC/unRarLicense.txt" "$INSTALL/share/licenses/7zip/unRarLicense.txt"

# --- verify -----------------------------------------------------------------
log "checking exported symbols"
syms="$(nm -D "$INSTALL/lib/7z.so" | grep -cE ' (CreateObject|GetNumberOfFormats|GetHandlerProperty2)$' || true)"
[[ $syms == 3 ]] || die "expected CreateObject, GetNumberOfFormats, GetHandlerProperty2 in 7z.so, found $syms of 3"

# Consumer check: find_package(7zip CONFIG) -> 7zip::7zip, compile against the
# installed headers, dlopen the installed 7z.so.
TESTDIR="$WORK/consumer-test"
log "consumer check"
cmake -S "$HERE/test" -B "$TESTDIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$INSTALL" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH >/dev/null
cmake --build "$TESTDIR" >/dev/null
count="$("$TESTDIR/check_7z" "$INSTALL/lib/7z.so")" || die "consumer check failed"
log "7z.so reports $count archive formats"
