#!/usr/bin/env bash
# Build the MO2 Linux port dependencies and application into ./install.
#
#   ./build.sh [--only <target>]... [--tests] [--list]
#
# Environment: MO2_SRC (default ../modorganizer) is the application fork.
# Works inside the build container and in any environment that provides the
# dependencies listed in build/Containerfile (CC/CXX/CMAKE_PREFIX_PATH from
# the environment are respected, e.g. after `source /opt/mo2env/activate-mo2.sh`).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MO2_SRC="${MO2_SRC:-$ROOT/../modorganizer}"
INSTALL="$ROOT/install"
BUILD_OUT="$ROOT/build-out"

# Build order. Each target <t> is built by a function build_<t> (dashes become
# underscores). A target without such a function is "not implemented yet".
ALL_TARGETS=(
    7zip cmake_common cmake-smoke uibase archive bsatk esptk lootcli dds-header
    modorganizer game_bethesda installer_quick installer_manual installer_fomod
)

ONLY=()
TESTS=0

die() { echo "build.sh: $*" >&2; exit 1; }
log() { echo "==> $*"; }

fn_of() { echo "build_${1//-/_}"; }
is_implemented() { declare -F "$(fn_of "$1")" >/dev/null; }
is_known() { local t; for t in "${ALL_TARGETS[@]}"; do [[ $t == "$1" ]] && return 0; done; return 1; }

# Prepend the install prefix to CMAKE_PREFIX_PATH (keeping what the environment gave us).
export CMAKE_PREFIX_PATH="$INSTALL${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"

# cmake_build <target> <source-dir> [extra configure args...]
# Generic CMake target: configure, build, (ctest with --tests), install.
cmake_build() {
    local name="$1" src="$2"; shift 2
    local bdir="$BUILD_OUT/$name" testing=OFF
    [[ $TESTS == 1 ]] && testing=ON
    [[ -d $src ]] || die "source directory for $name not found: $src"
    log "configure $name"
    cmake -S "$src" -B "$bdir" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_INSTALL_PREFIX="$INSTALL" \
        -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH//:/;}" \
        -DBUILD_TESTING="$testing" "$@"
    log "build $name"
    cmake --build "$bdir"
    if [[ $TESTS == 1 ]]; then
        log "test $name"
        ctest --test-dir "$bdir" --output-on-failure --no-tests=ignore
    fi
    log "install $name"
    cmake --install "$bdir"
}

# --- per-target build steps (add new ones as build_<target> functions) -------

# cmake_common is a set of CMake modules, no build: copy them like the vcpkg port does.
build_cmake_common() {
    local dest="$INSTALL/share/mo2-cmake" f
    mkdir -p "$dest"
    for f in "$ROOT"/deps/cmake_common/*.cmake; do
        install -m 0644 "$f" "$dest/"
    done
}

# cmake-smoke consumes the installed cmake_common modules (find_package(mo2-cmake)) to
# build a tiny C++23 project; the executable must run and print "ok".
build_cmake_smoke() {
    # throwaway install prefix: the smoke project must not leave files in $INSTALL
    cmake_build cmake-smoke "$ROOT/tests/cmake-smoke" \
        -DCMAKE_INSTALL_PREFIX="$BUILD_OUT/cmake-smoke-install"
    local out
    log "run cmake-smoke"
    out="$("$BUILD_OUT/cmake-smoke/smoke_main")" || die "cmake-smoke executable failed"
    [[ $out == ok ]] || die "cmake-smoke printed '$out', expected 'ok'"
    log "cmake-smoke: $out"
}

# --- driver -----------------------------------------------------------------

while (($#)); do
    case "$1" in
        --only) (($# >= 2)) || die "--only needs a target"; ONLY+=("$2"); shift 2 ;;
        --only=*) ONLY+=("${1#--only=}"); shift ;;
        --tests) TESTS=1; shift ;;
        --list) printf '%s\n' "${ALL_TARGETS[@]}"; exit 0 ;;
        -h|--help) sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
done

for t in "${ONLY[@]}"; do
    is_known "$t" || die "unknown target: $t (see --list)"
    is_implemented "$t" || die "target $t not implemented yet"
done

mkdir -p "$INSTALL"
skipped=()
for t in "${ALL_TARGETS[@]}"; do
    if ((${#ONLY[@]})); then
        [[ " ${ONLY[*]} " == *" $t "* ]] || continue
    elif ! is_implemented "$t"; then
        skipped+=("$t"); continue
    fi
    log "target $t"
    "$(fn_of "$t")"
done
((${#skipped[@]})) && log "skipped (not implemented yet): ${skipped[*]}"
log "done; install prefix: $INSTALL"
