#!/usr/bin/env bash
# Build libbsarchpp 1.0.7 (Kaedras/libbsarchpp, a C++ reimplementation of xEdit's
# BSArchive) as a static, position-independent library with its headers and CMake
# package into <root>/install, and install the boost-ext/di header (boost/di.hpp) that
# bsapacker needs; Fedora packages neither (spec of Phase 4e, section 2). The bsapacker
# plugin links libbsarchpp on Linux in place of libbsarch, which is Windows-only.
#
#   build/libbsarchpp/build-libbsarchpp.sh
#
# Environment (all optional):
#   INSTALL    install prefix            (default <root>/install)
#   BUILD_OUT  build tree root           (default <root>/build-out)
#   TESTS      1: build and run the tests of libbsarchpp that need no game data
#
# Everything it fetches and builds stays in $BUILD_OUT/libbsarchpp.
#
# Produces:
#   $INSTALL/lib/libbsarchpp.a, $INSTALL/include/libbsarchpp/*.h
#   $INSTALL/lib/cmake/libbsarchpp/   (find_package(libbsarchpp CONFIG), target libbsarchpp)
#   $INSTALL/include/boost/di.hpp
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
INSTALL="${INSTALL:-$ROOT/install}"
BUILD_OUT="${BUILD_OUT:-$ROOT/build-out}"
TESTS="${TESTS:-0}"

LIBBSARCHPP_REPO="https://github.com/Kaedras/libbsarchpp"
LIBBSARCHPP_TAG="1.0.7"
# Commit the tag must resolve to (the tag is moveable, the commit is not).
LIBBSARCHPP_COMMIT="211a681cf89310af5c3f824315ba03eb64651a95"
DI_REPO="https://github.com/boost-ext/di"
DI_TAG="v1.3.2"
DI_COMMIT="20b6454c889f23c1e7c9eb5136cd7e3d6f6d718d"

WORK="$BUILD_OUT/libbsarchpp"
SRC="$WORK/src"
DI_SRC="$WORK/di"
OBJ="$WORK/build"

log() { echo "==> libbsarchpp: $*"; }
die() { echo "build-libbsarchpp.sh: $*" >&2; exit 1; }

# fetch <repo> <tag> <commit> <dir>: shallow clone of the tag, verified by commit hash
# (cached; safe.directory as in build/libloot/build-libloot.sh)
head_of() { git -c safe.directory="$1" -C "$1" rev-parse HEAD 2>/dev/null || true; }
fetch() {
    local repo="$1" tag="$2" commit="$3" dir="$4"
    if [[ $(head_of "$dir") != "$commit" ]]; then
        log "fetching $repo @ $tag"
        rm -rf "$dir"
        mkdir -p "$WORK"
        git clone --quiet --depth 1 --branch "$tag" -c advice.detachedHead=false "$repo" "$dir"
        [[ $(head_of "$dir") == "$commit" ]] ||
            die "tag $tag of $repo resolved to $(head_of "$dir"), expected $commit"
        rm -rf "$OBJ"
    fi
}
fetch "$LIBBSARCHPP_REPO" "$LIBBSARCHPP_TAG" "$LIBBSARCHPP_COMMIT" "$SRC"
fetch "$DI_REPO" "$DI_TAG" "$DI_COMMIT" "$DI_SRC"
log "source at $LIBBSARCHPP_COMMIT, boost-ext/di at $DI_COMMIT"

# our package config (see config.cmake.in); restored from git on every run so that
# the source tree stays the fetched one
git -c safe.directory="$SRC" -C "$SRC" checkout --quiet -- cmake/config.cmake.in
install -m 0644 "$HERE/config.cmake.in" "$SRC/cmake/config.cmake.in"

# --- build ------------------------------------------------------------------
testing=OFF
[[ $TESTS == 1 ]] && testing=ON
games=()
for g in TES3 TES4 TES5 SSE SKYRIMVR FO3 FONV FO4 FO4VR SF; do
    games+=("-DTESTS_${g}_ENABLE=OFF")
done
log "configure"
cmake -S "$SRC" -B "$OBJ" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX="$INSTALL" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DLIBBSARCHPP_TESTING="$testing" \
    -DTESTS_WORKDIR="$WORK/testwork" \
    "${games[@]}"
log "build"
cmake --build "$OBJ"
if [[ $TESTS == 1 ]]; then
    # the suites per game need the games' archives (/opt/archives) and are off; what is
    # left needs no game data (Misc, Hash, GSL, CreateArchive). The BsaTest
    # instantiations are all behind TESTS_<GAME>_ENABLE, which is turned off above, so
    # gtest's uninstantiated-suite self-check (GoogleTestVerification) would always fail;
    # every real test still runs.
    mkdir -p "$WORK/testwork"
    log "test"
    ctest --test-dir "$OBJ" --output-on-failure --exclude-regex '^GoogleTestVerification\.'
fi
log "install into $INSTALL"
cmake --install "$OBJ"
install -Dm 0644 "$DI_SRC/include/boost/di.hpp" "$INSTALL/include/boost/di.hpp"

# --- check ------------------------------------------------------------------
[[ -f $INSTALL/lib/libbsarchpp.a ]] || die "libbsarchpp.a was not installed"
compgen -G "$INSTALL/lib/cmake/libbsarchpp/*onfig.cmake" >/dev/null ||
    die "the CMake package of libbsarchpp was not installed"
if [[ $TESTS == 1 ]]; then
    # the installed package, on its own: found, linked into a shared object, and the
    # archive it writes reads back
    log "consumer check"
    rm -rf "$WORK/consumer" "$WORK/consumer-run"
    cmake -S "$HERE/consumer" -B "$WORK/consumer" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH="$INSTALL"
    cmake --build "$WORK/consumer"
    mkdir -p "$WORK/consumer-run"
    out="$("$WORK/consumer/consumer" "$WORK/consumer-run")" || die "consumer check failed: $out"
    [[ $out == ok ]] || die "consumer printed '$out', expected 'ok'"
    rm -rf "$WORK/consumer-run"
    log "consumer check: ok"
fi
log "installed $INSTALL/lib/libbsarchpp.a"
