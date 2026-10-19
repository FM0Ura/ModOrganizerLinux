#!/usr/bin/env bash
# Build libloot 0.29.5, the C++ wrapper (cpp/ of the libloot repository) of the Rust
# library, as the shared library libloot.so and install it with its headers and CMake
# package into <root>/install. LOOT publishes binaries for Windows only (spec of
# Phase 4b, section 1.1); this is the version of the libloot port of the MO2 vcpkg
# registry, which the Windows build uses.
#
#   build/libloot/build-libloot.sh
#
# Environment (all optional):
#   INSTALL    install prefix            (default <root>/install)
#   BUILD_OUT  build tree root           (default <root>/build-out)
#
# Everything it fetches and builds stays in $BUILD_OUT/libloot (source, cargo home with
# the crates, build tree), which CI caches.
#
# Produces:
#   $INSTALL/lib/libloot.so.0 (and libloot.so)
#   $INSTALL/include/loot/**.h
#   $INSTALL/lib/cmake/libloot/   (find_package(libloot CONFIG), libloot::libloot)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
INSTALL="${INSTALL:-$ROOT/install}"
BUILD_OUT="${BUILD_OUT:-$ROOT/build-out}"

LIBLOOT_REPO="https://github.com/loot/libloot"
LIBLOOT_TAG="0.29.5"
# Commit the tag must resolve to (the tag is moveable, the commit is not).
LIBLOOT_COMMIT="a12fcfcef21b96ac6fcffaac06439d11e18db932"

WORK="$BUILD_OUT/libloot"
SRC="$WORK/src"
OBJ="$WORK/build"

log() { echo "==> libloot: $*"; }
die() { echo "build-libloot.sh: $*" >&2; exit 1; }

# --- fetch (cached in build-out/libloot/src, verified by commit hash) -------
head_of() { git -C "$SRC" rev-parse HEAD 2>/dev/null || true; }

if [[ $(head_of) != "$LIBLOOT_COMMIT" ]]; then
    log "fetching $LIBLOOT_REPO @ $LIBLOOT_TAG"
    rm -rf "$SRC" "$OBJ"
    mkdir -p "$WORK"
    git clone --quiet --depth 1 --branch "$LIBLOOT_TAG" -c advice.detachedHead=false \
        "$LIBLOOT_REPO" "$SRC"
    [[ $(head_of) == "$LIBLOOT_COMMIT" ]] ||
        die "tag $LIBLOOT_TAG resolved to $(head_of), expected $LIBLOOT_COMMIT"
fi
log "source at $LIBLOOT_COMMIT"

# --- build ------------------------------------------------------------------
# cargo (run by the CMake project of cpp/) keeps its crates here, so that a cached
# build-out/libloot needs no download; the revision is embedded in the library
export CARGO_HOME="$WORK/cargo"
export LIBLOOT_REVISION="${LIBLOOT_COMMIT:0:7}"
log "configure"
cmake -S "$SRC/cpp" -B "$OBJ" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX="$INSTALL" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DLIBLOOT_BUILD_TESTS=OFF \
    -DLIBLOOT_INSTALL_DOCS=OFF
log "build"
cmake --build "$OBJ"
log "install into $INSTALL"
cmake --install "$OBJ"

# --- check ------------------------------------------------------------------
lib="$(readlink -f "$INSTALL/lib/libloot.so")" || die "libloot.so was not installed"
soname="$(readelf -d "$lib" | sed -n 's/.*(SONAME).*\[\(.*\)\]/\1/p')"
[[ $soname == libloot.so.0 ]] || die "unexpected soname '$soname' (lootcli expects libloot.so.0, see build.sh)"
compgen -G "$INSTALL/lib/cmake/libloot/*onfig.cmake" >/dev/null ||
    die "the CMake package of libloot was not installed"
log "installed $lib ($soname)"
