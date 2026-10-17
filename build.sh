#!/usr/bin/env bash
# Build the MO2 Linux port dependencies and application into ./install.
#
#   ./build.sh [--only <target>]... [--allow-fail <target>]... [--tests] [--list]
#
# --allow-fail <target>: a failure of that target is reported but does not stop the
# build or fail the run (used by CI for targets that are still being ported); the
# modorganizer unit tests (--tests) are not covered by it.
#
# Environment: MO2_SRC (default deps/modorganizer) is the application source.
# Works inside the build container and in any environment that provides the
# dependencies listed in build/Containerfile (CC/CXX/CMAKE_PREFIX_PATH from
# the environment are respected, e.g. after `source /opt/mo2env/activate-mo2.sh`).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MO2_SRC="${MO2_SRC:-$ROOT/deps/modorganizer}"
INSTALL="$ROOT/install"
BUILD_OUT="$ROOT/build-out"

# Build order. Each target <t> is built by a function build_<t> (dashes become
# underscores). A target without such a function is "not implemented yet". An optional
# function pre_<t> runs first, outside --allow-fail.
ALL_TARGETS=(
    7zip cmake_common cmake-smoke uibase archive dds-header bsatk esptk lootcli
    mo2-vfs modorganizer game_bethesda installer_quick installer_manual installer_fomod
)

ONLY=()
ALLOW_FAIL=()
TESTS=0

die() { echo "build.sh: $*" >&2; exit 1; }
log() { echo "==> $*"; }

fn_of() { echo "build_${1//-/_}"; }
is_implemented() { declare -F "$(fn_of "$1")" >/dev/null; }
is_known() { local t; for t in "${ALL_TARGETS[@]}"; do [[ $t == "$1" ]] && return 0; done; return 1; }

# Prepend the install prefix to CMAKE_PREFIX_PATH (keeping what the environment gave us).
export CMAKE_PREFIX_PATH="$INSTALL${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"

# cmake_configure <target> <source-dir> [extra configure args...]
# Configures $BUILD_OUT/<target>; BUILD_TESTING follows --tests.
# CMAKE_INSTALL_LIBDIR is pinned to lib: Fedora would default to lib64, but the
# install tree keeps every library in lib/ (the application's RPATH is $ORIGIN/../lib).
cmake_configure() {
    local name="$1" src="$2"; shift 2
    local bdir="$BUILD_OUT/$name" testing=OFF
    [[ $TESTS == 1 ]] && testing=ON
    [[ -d $src ]] || die "source directory for $name not found: $src"
    log "configure $name"
    cmake -S "$src" -B "$bdir" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_INSTALL_PREFIX="$INSTALL" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH//:/;}" \
        -DBUILD_TESTING="$testing" "$@"
}

# cmake_build <target> <source-dir> [extra configure args...]
# Generic CMake target: configure, build, (ctest with --tests), install.
cmake_build() {
    local name="$1" src="$2"; shift 2
    local bdir="$BUILD_OUT/$name"
    cmake_configure "$name" "$src" "$@"
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

# 7-Zip is built with its own makefiles; the logic lives in build/7zip/build-7zip.sh.
build_7zip() {
    INSTALL="$INSTALL" BUILD_OUT="$BUILD_OUT" "$ROOT/build/7zip/build-7zip.sh"
}

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

# uibase is the plugin API / shared UI library; with --tests its GoogleTest suite runs
# (upstream tests and the Linux implementations of the Win32-backed functions).
build_uibase() {
    cmake_build uibase "$ROOT/deps/uibase"
}

# archive is the 7-Zip wrapper; with --tests its GoogleTest suite runs against 7z.so.
build_archive() {
    cmake_build archive "$ROOT/deps/archive"
}

# dds-header: the vcpkg port mo2-dds-header installs DDS.h into include/DDS and a
# hand-written config exporting mo2::dds-header. DDS.h comes from the vendored DirectXTex
# (CRLF in the working tree, so it is normalised and checked against the port's SHA-512).
# Windows gets dxgiformat.h from the SDK, where DDS.h users expect it; on Linux the
# package ships a replacement next to DDS.h (build/dds-header/dxgiformat.h).
DDS_H_SHA512=49ED2C454A39D59EBAE06F2E96E814246754C83ECE074E052A8872F7E16FA7C9E00FEC3B1F1757E09AE9FC1D7C7AFCF7C845D377A3E8EC876E7F7D5CEDB1487A
build_dds_header() {
    local src="$ROOT/deps/dds-header/DirectXTex/DDS.h" dest="$INSTALL/include/DDS" sum
    [[ -f $src ]] || die "DDS.h not found: $src"
    mkdir -p "$dest" "$INSTALL/share/mo2-dds-header"
    tr -d '\r' < "$src" > "$dest/DDS.h"
    sum="$(sha512sum "$dest/DDS.h" | cut -d' ' -f1)"
    [[ ${sum^^} == "$DDS_H_SHA512" ]] || die "DDS.h does not match the mo2-dds-header port (sha512 $sum)"
    install -m 0644 "$ROOT/build/dds-header/dxgiformat.h" "$dest/"
    install -m 0644 "$ROOT/build/dds-header/mo2-dds-header-config.cmake" "$INSTALL/share/mo2-dds-header/"
}

# bsatk reads/extracts BSA archives; with --tests its GoogleTest suite runs against a
# synthetic Skyrim SE archive. It needs find_package(lz4 CONFIG), which distribution
# packages do not provide: install a small config for the system lz4 first.
build_bsatk() {
    mkdir -p "$INSTALL/lib/cmake/lz4"
    install -m 0644 "$ROOT/build/lz4/lz4Config.cmake" "$INSTALL/lib/cmake/lz4/"
    cmake_build bsatk "$ROOT/deps/bsatk"
}

# esptk reads plugin headers; with --tests its GoogleTest suite runs against synthetic plugins.
build_esptk() {
    cmake_build esptk "$ROOT/deps/esptk"
}

# lootcli: only the header package mo2-lootcli-header (mo2::lootcli-header) is built, like
# the vcpkg port. The lootcli executable needs Windows-only libloot and is skipped by
# making Qt6 invisible to the project (its CMake builds the executable only if Qt6 is found).
build_lootcli() {
    cmake_build lootcli "$ROOT/deps/lootcli" -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
}

# mo2-vfs is the FUSE virtual file system (Rust, ./mo2-vfs), a program of its own
# started by the application; with --tests its unit and integration tests run (the
# ones that need FUSE are skipped without /dev/fuse, unless MO2_REQUIRE_FUSE=1)
build_mo2_vfs() {
    local src="$ROOT/mo2-vfs"
    # the toolchain image keeps CARGO_HOME read-only for non-root users (distrobox)
    [[ -w ${CARGO_HOME:-$HOME/.cargo} ]] || export CARGO_HOME="$HOME/.cache/mo2-cargo"
    log "build mo2-vfs"
    cargo build --release --locked --manifest-path "$src/Cargo.toml" \
        --target-dir "$BUILD_OUT/mo2-vfs"
    if [[ $TESTS == 1 ]]; then
        cargo test --release --locked --manifest-path "$src/Cargo.toml" \
            --target-dir "$BUILD_OUT/mo2-vfs"
    fi
    install -Dm755 "$BUILD_OUT/mo2-vfs/release/mo2-vfs" "$INSTALL/bin/mo2-vfs"
}

# modorganizer is the application itself ($MO2_SRC, by default deps/modorganizer),
# configured with its linux-gcc preset (Ninja, RelWithDebInfo, no vcpkg); -B keeps the
# build tree out of the source. It installs bin/ModOrganizer with its resources next to it; with --tests,
# the installed executable must then pass the smoke test.
build_modorganizer() {
    cmake_build modorganizer "$MO2_SRC" --preset linux-gcc
    [[ $TESTS == 1 ]] && smoke_modorganizer
    return 0
}

# smoke test (tests/smoke/modorganizer-smoke.sh, also run by CI): `ModOrganizer --help`
# prints the usage, and the application started headless with a fresh HOME gets past
# its QApplication to the instance selection (it loads the plugins) without crashing
smoke_modorganizer() {
    log "smoke test: ModOrganizer"
    "$ROOT/tests/smoke/modorganizer-smoke.sh" "$INSTALL" || die "smoke test failed"
    log "smoke test passed"
}

# With --tests, the unit tests of the application (organizer_tests: the data tree and
# the Linux env layer, which build without the rest of the application) are built and
# run before the application (with MO2_VFS_BINARY set to the installed mo2-vfs). This
# is a pre_ step: it runs even when the application itself is in --allow-fail, and its
# failure always fails the run.
pre_modorganizer() {
    [[ $TESTS == 1 ]] || return 0
    cmake_configure modorganizer "$MO2_SRC" --preset linux-gcc
    log "build organizer_tests"
    cmake --build "$BUILD_OUT/modorganizer" --target organizer_tests
    log "test organizer_tests"
    # the tests of the virtual file system connector run the helper built by mo2-vfs
    # (installed before this step)
    export MO2_VFS_BINARY="$INSTALL/bin/mo2-vfs"
    ctest --test-dir "$BUILD_OUT/modorganizer" --output-on-failure
}

# check_plugins <game|installer> <plugin>...: with --tests, the plugin load check
# (tests/plugin-load) loads the installed plugins with QPluginLoader, like the
# application's PluginContainer, and checks the interface each one implements.
check_plugins() {
    [[ $TESTS == 1 ]] || return 0
    local kind="$1"; shift
    # throwaway install prefix: the check must not leave files in $INSTALL
    cmake_configure plugin-load "$ROOT/tests/plugin-load" \
        -DCMAKE_INSTALL_PREFIX="$BUILD_OUT/plugin-load-install"
    cmake --build "$BUILD_OUT/plugin-load"
    log "plugin load check ($kind)"
    "$BUILD_OUT/plugin-load/plugin_load" "$kind" "$@" || die "plugin load check failed"
}

# game_bethesda: the plugins of the Bethesda games (one libgame_<game>.so per game, built
# on the static libraries game_gamebryo and game_creation), installed in bin/plugins;
# with --tests its GoogleTest suite runs (Skyrim SE: detection in a fake Steam library,
# data directory, save information) and every game plugin must load.
build_game_bethesda() {
    cmake_build game_bethesda "$ROOT/deps/game_bethesda"
    check_plugins game "$INSTALL"/bin/plugins/libgame_*.so
}

# the installer plugins, installed in bin/plugins; with --tests each must load, and the
# GoogleTest suite of installer_fomod parses the FOMOD sample of tests/fixtures.
build_installer_quick() {
    cmake_build installer_quick "$ROOT/deps/installer_quick"
    check_plugins installer "$INSTALL/bin/plugins/libinstaller_quick.so"
}

build_installer_manual() {
    cmake_build installer_manual "$ROOT/deps/installer_manual"
    check_plugins installer "$INSTALL/bin/plugins/libinstaller_manual.so"
}

build_installer_fomod() {
    cmake_build installer_fomod "$ROOT/deps/installer_fomod" \
        -DFOMOD_SAMPLE_DIR="$ROOT/tests/fixtures/fomod-sample"
    check_plugins installer "$INSTALL/bin/plugins/libinstaller_fomod.so"
}

# --- driver -----------------------------------------------------------------

while (($#)); do
    case "$1" in
        --only) (($# >= 2)) || die "--only needs a target"; ONLY+=("$2"); shift 2 ;;
        --only=*) ONLY+=("${1#--only=}"); shift ;;
        --allow-fail) (($# >= 2)) || die "--allow-fail needs a target"; ALLOW_FAIL+=("$2"); shift 2 ;;
        --allow-fail=*) ALLOW_FAIL+=("${1#--allow-fail=}"); shift ;;
        --tests) TESTS=1; shift ;;
        --list) printf '%s\n' "${ALL_TARGETS[@]}"; exit 0 ;;
        -h|--help) sed -n '2,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
done

for t in "${ONLY[@]}"; do
    is_known "$t" || die "unknown target: $t (see --list)"
    is_implemented "$t" || die "target $t not implemented yet"
done
for t in "${ALLOW_FAIL[@]}"; do
    is_known "$t" || die "unknown target: $t (see --list)"
done

# The unit tests and the smoke test run without a display, whatever the environment says
# (the container image and a desktop session may set QT_QPA_PLATFORM to something else).
((TESTS)) && export QT_QPA_PLATFORM=offscreen

mkdir -p "$INSTALL"
skipped=()
failed=()
for t in "${ALL_TARGETS[@]}"; do
    if ((${#ONLY[@]})); then
        [[ " ${ONLY[*]} " == *" $t "* ]] || continue
    elif ! is_implemented "$t"; then
        skipped+=("$t"); continue
    fi
    log "target $t"
    # optional pre_<target> step, never covered by --allow-fail
    if declare -F "pre_${t//-/_}" >/dev/null; then
        "pre_${t//-/_}"
    fi
    if [[ " ${ALLOW_FAIL[*]} " == *" $t "* ]]; then
        # run in a subshell outside any condition so that set -e still applies inside
        set +e
        ( set -e; "$(fn_of "$t")" )
        rc=$?
        set -e
        if ((rc != 0)); then
            log "target $t FAILED (exit $rc), allowed by --allow-fail; continuing"
            failed+=("$t")
        fi
    else
        "$(fn_of "$t")"
    fi
done
((${#skipped[@]})) && log "skipped (not implemented yet): ${skipped[*]}"
((${#failed[@]})) && log "failed (allowed): ${failed[*]}"
[[ ! -e $INSTALL/lib64 ]] || die "$INSTALL/lib64 exists: libraries must install into lib/"
log "done; install prefix: $INSTALL"
