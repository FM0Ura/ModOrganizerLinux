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
    7zip cmake_common cmake-smoke uibase archive dds-header bsatk esptk libloot lootcli
    mo2-vfs modorganizer game_bethesda installer_quick installer_manual installer_fomod
    installer_bain installer_bundle preview_base preview_bsa bsa_extractor diagnose_basic
    check_fnis tool_inieditor tool_inibakery plugin_python
    preview_dds form43_checker script_extender_plugin_checker tool_configurator
    installer_wizard fnistool basic_games
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
    [[ -f "$BUILD_OUT/cmake-smoke/form.py" ]] ||
        die "cmake-smoke: pyuic6 did not generate form.py"
    grep -q "class Ui_Form" "$BUILD_OUT/cmake-smoke/form.py" ||
        die "cmake-smoke: form.py is not a pyuic6 output"
    log "cmake-smoke: python ok"
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
# packages do not provide: install a small config for the system lz4 first. It is a static
# library that the preview_bsa and bsa_extractor plugins link into shared objects, hence PIC.
build_bsatk() {
    mkdir -p "$INSTALL/lib/cmake/lz4"
    install -m 0644 "$ROOT/build/lz4/lz4Config.cmake" "$INSTALL/lib/cmake/lz4/"
    cmake_build bsatk "$ROOT/deps/bsatk" -DCMAKE_POSITION_INDEPENDENT_CODE=ON
}

# esptk reads plugin headers; with --tests its GoogleTest suite runs against synthetic plugins.
build_esptk() {
    cmake_build esptk "$ROOT/deps/esptk"
}

# libloot is LOOT's library (Rust, with a C++ wrapper), needed by the lootcli executable;
# the logic lives in build/libloot/build-libloot.sh (source pinned to 0.29.5).
build_libloot() {
    INSTALL="$INSTALL" BUILD_OUT="$BUILD_OUT" "$ROOT/build/libloot/build-libloot.sh"
}

# lootcli: the executable (deps/lootcli on libloot, installed in bin/loot with the
# libloot.so.0 it needs, run path $ORIGIN) and the header package mo2-lootcli-header
# (mo2::lootcli-header). Mod Organizer starts it through mo2-vfs, which runs on the
# host when Mod Organizer runs in the mo2-run box (spec of Phase 4b, section 1.2): it
# must need nothing but libloot and libraries the host (Bazzite, Fedora 44) has,
# checked here against LOOTCLI_HOST_LIBS. With --tests, its tests run (ctest) and it
# must reject a missing argument.
LOOTCLI_HOST_LIBS="libloot.so.0 libQt6Core.so.6 libboost_locale.so.1.90.0 libcurl.so.4 libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6"
build_lootcli() {
    # -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=OFF: a build-out/lootcli left by an earlier phase
    # keeps Qt6 disabled in its cache, and the executable would not be configured
    cmake_build lootcli "$ROOT/deps/lootcli" -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=OFF
    local exe="$INSTALL/bin/loot/lootcli" lib out
    [[ -x $exe ]] || die "lootcli was not installed in $INSTALL/bin/loot"
    [[ -e $INSTALL/bin/loot/libloot.so.0 ]] || die "libloot.so.0 is not next to lootcli"
    readelf -d "$exe" | grep -Eq '\((RUNPATH|RPATH)\).*\[\$ORIGIN\]' ||
        die "lootcli does not have the run path \$ORIGIN"
    for lib in $(readelf -d "$exe" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
        [[ " $LOOTCLI_HOST_LIBS " == *" $lib "* ]] ||
            die "lootcli needs $lib, which is not in LOOTCLI_HOST_LIBS (the host may not have it)"
    done
    if [[ $TESTS == 1 ]]; then
        out="$("$exe" --game SkyrimSE 2>&1)" && die "lootcli without --gamePath exited with 0"
        [[ $out == *"argument missing gamePath"* ]] || die "lootcli printed '$out'"
        log "lootcli: rejects a missing argument"
    fi
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
# the installed executable must then pass the smoke test (smoke_modorganizer, after the
# last target).
build_modorganizer() {
    cmake_build modorganizer "$MO2_SRC" --preset linux-gcc
}

# With --tests, after every target (like vfs_e2e, below): the smoke test
# (tests/smoke/modorganizer-smoke.sh; CI runs it only through here): `ModOrganizer --help`
# prints the usage, the application started headless with a fresh HOME gets past its
# QApplication to the instance selection (it loads the plugins) without crashing, and
# with a ready Skyrim SE instance it gets up to the main window. That last step needs
# the game plugin (game_bethesda, built after the application), hence after the last
# target. It runs in a full build, and with --only when modorganizer or game_bethesda
# was built and the application is installed; without an installed Skyrim SE plugin
# (--only modorganizer in a clean tree, or game_bethesda failed under --allow-fail) the
# main window step is skipped with a message.
smoke_modorganizer() {
    [[ $TESTS == 1 ]] || return 0
    local main=1
    if ((${#ONLY[@]})); then
        [[ " ${ONLY[*]} " == *" modorganizer "* || " ${ONLY[*]} " == *" game_bethesda "* ]] ||
            return 0
        if [[ ! -x $INSTALL/bin/ModOrganizer ]]; then
            log "smoke test skipped: build modorganizer first"
            return 0
        fi
    fi
    if [[ " ${failed[*]} " == *" game_bethesda "* ||
        ! -f $INSTALL/bin/plugins/libgame_skyrimse.so ]]; then
        log "smoke test: the Skyrim SE plugin is not available (game_bethesda), main window step skipped"
        main=0
    fi
    log "smoke test: ModOrganizer"
    MO2_SMOKE_MAIN_WINDOW=$main "$ROOT/tests/smoke/modorganizer-smoke.sh" "$INSTALL" ||
        die "smoke test failed"
    log "smoke test passed"
}

# With --tests, after every target: the end-to-end test of the virtual file system
# (tests/vfs-e2e/run.sh, see there): the installed application runs a native program
# of a test instance with the mo2-vfs view of the game's data directory. It needs the
# application, mo2-vfs and the Skyrim SE plugin (game_bethesda, built after the
# application), so it runs after the last of them: always in a full build, and with
# --only when one of them was built and the others are installed already. Without
# /dev/fuse it is skipped with a message, unless MO2_REQUIRE_FUSE=1.
vfs_e2e() {
    [[ $TESTS == 1 ]] || return 0
    if ((${#ONLY[@]})); then
        [[ " ${ONLY[*]} " == *" mo2-vfs "* || " ${ONLY[*]} " == *" modorganizer "* ||
            " ${ONLY[*]} " == *" game_bethesda "* ]] || return 0
        if [[ ! -x $INSTALL/bin/ModOrganizer || ! -x $INSTALL/bin/mo2-vfs ||
            ! -f $INSTALL/bin/plugins/libgame_skyrimse.so ]]; then
            log "vfs end-to-end test skipped: build modorganizer, mo2-vfs and game_bethesda first"
            return 0
        fi
    fi
    log "vfs end-to-end test"
    "$ROOT/tests/vfs-e2e/run.sh" "$INSTALL" || die "vfs end-to-end test failed"
    log "vfs end-to-end test passed"
}

# With --tests, after every target: the end-to-end test of LOOT (tests/loot-e2e/run.sh,
# see there): lootcli, started by mo2-vfs with the virtual file system mounted, sorts
# the plugins of the mods of a fake Skyrim SE. It needs mo2-vfs and lootcli: always in
# a full build, and with --only when libloot, lootcli or mo2-vfs was built and both are
# installed.
loot_e2e() {
    [[ $TESTS == 1 ]] || return 0
    if ((${#ONLY[@]})); then
        [[ " ${ONLY[*]} " == *" libloot "* || " ${ONLY[*]} " == *" lootcli "* ||
            " ${ONLY[*]} " == *" mo2-vfs "* ]] || return 0
        if [[ ! -x $INSTALL/bin/mo2-vfs || ! -x $INSTALL/bin/loot/lootcli ]]; then
            log "loot end-to-end test skipped: build mo2-vfs and lootcli first"
            return 0
        fi
    fi
    log "loot end-to-end test"
    "$ROOT/tests/loot-e2e/run.sh" "$INSTALL" "$BUILD_OUT/testing-plugins" ||
        die "loot end-to-end test failed"
    log "loot end-to-end test passed"
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

# check_plugins <kind> <plugin>...: kind is a type of tests/plugin-load (game, installer, preview, tool, diagnose, filemapper or plugin); with --tests, the plugin load check
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

# the other installer plugins of upstream (Phase 4c.1): BAIN packages and archives
# that contain other archives
build_installer_bain() {
    cmake_build installer_bain "$ROOT/deps/installer_bain"
    check_plugins installer "$INSTALL/bin/plugins/libinstaller_bain.so"
}

build_installer_bundle() {
    cmake_build installer_bundle "$ROOT/deps/installer_bundle"
    check_plugins installer "$INSTALL/bin/plugins/libinstaller_bundle.so"
}

# the preview plugins (Phase 4c.1): text, images and INI files, and the contents of
# BSA/BA2 archives (bsatk)
build_preview_base() {
    cmake_build preview_base "$ROOT/deps/preview_base"
    check_plugins preview "$INSTALL/bin/plugins/libpreview_base.so"
}

build_preview_bsa() {
    cmake_build preview_bsa "$ROOT/deps/preview_bsa"
    check_plugins preview "$INSTALL/bin/plugins/libpreview_bsa.so"
}

# bsa_extractor (Phase 4c.1): offers to extract the BSA archives of a mod when it is
# installed (bsatk)
build_bsa_extractor() {
    cmake_build bsa_extractor "$ROOT/deps/bsa_extractor"
    check_plugins plugin "$INSTALL/bin/plugins/libbsa_extractor.so"
}

# diagnose_basic (Phase 4c.1): the basic checks of Mod Organizer (overwrite, error log,
# fonts, missing masters, ...); its check of NTFS file attributes is Windows-only (spec
# of Phase 4c.1, section 1.2), so its setting must not be offered on Linux
build_diagnose_basic() {
    cmake_build diagnose_basic "$ROOT/deps/diagnose_basic"
    check_plugins diagnose "$INSTALL/bin/plugins/libdiagnose_basic.so"
    if [[ $TESTS == 1 ]]; then
        local out
        out="$("$BUILD_OUT/plugin-load/plugin_load" diagnose \
            "$INSTALL/bin/plugins/libdiagnose_basic.so")"
        [[ $out != *check_fileattributes* ]] ||
            die "diagnose_basic offers check_fileattributes on Linux"
    fi
}

# check_fnis (Phase 4c.1): before the game runs, offers to run FNIS when its data changed;
# FNIS (a Windows program) runs through IOrganizer::startApplication() like Run
build_check_fnis() {
    cmake_build check_fnis "$ROOT/deps/check_fnis"
    check_plugins plugin "$INSTALL/bin/plugins/libcheck_fnis.so"
}

# the INI tools (Phase 4c.1): the INI Editor (Tools menu; its external mode opens the
# INI files with the desktop's handler on Linux) and the INI Bakery, which maps the
# INI files of the profile into the virtual file system
build_tool_inieditor() {
    cmake_build tool_inieditor "$ROOT/deps/tool_inieditor"
    check_plugins tool "$INSTALL/bin/plugins/libinieditor.so"
}

build_tool_inibakery() {
    cmake_build tool_inibakery "$ROOT/deps/tool_inibakery"
    check_plugins filemapper "$INSTALL/bin/plugins/libinibakery.so"
}

# plugin_python (Phase 4d.1): the Python proxy (bin/plugins/plugin_python.so), its runner
# and the mobase module (bin/plugins/plugin_python/), with the system's Python 3.14 and
# PyQt6; with --tests its suites run (the runner's GoogleTest suite, with the filter of
# sys.path and the isolation from the user's packages, and the pytest suite of mobase),
# the proxy must load and mobase must import in the system's Python; the proxy must not
# need libpython nor the runner (it loads them at run time, so that a missing Python is
# reported by the plugin instead of failing its load)
build_plugin_python() {
    cmake_build plugin_python "$ROOT/deps/plugin_python"
    check_plugins plugin "$INSTALL/bin/plugins/plugin_python.so"
    if [[ $TESTS == 1 ]]; then
        local needed
        needed="$(LC_ALL=C readelf -d "$INSTALL/bin/plugins/plugin_python.so" | grep NEEDED)" ||
            die "plugin_python.so: readelf found no NEEDED entry"
        if grep -E "libpython|librunner" <<<"$needed"; then
            die "plugin_python.so needs libpython or librunner (above): it must load them at run time"
        fi
        LD_LIBRARY_PATH="$INSTALL/lib" python3 -I "$ROOT/tests/python-import/check_mobase.py" \
            "$INSTALL" || die "mobase does not import"
    fi
}

# the Python plugins of Mod Organizer 2.5 (Phase 4d.1), installed in bin/plugins: the DDS
# preview, the Form 43 and script extender plugin checkers, the INI configurator, the
# installer of BAIN archives with wizard scripts and the FNIS tool; with --tests each
# must compile in the system's Python (python3 -m py_compile) after installation
python_plugin() {
    local target="$1"; shift
    cmake_build "$target" "$ROOT/deps/$target"
    [[ $TESTS == 1 ]] || return 0
    # the compiled files go to a scratch folder: the installed plugins stay as installed
    local f cache="$BUILD_OUT/$target/pycache"
    for f in "$@"; do
        [[ -e $INSTALL/bin/plugins/$f ]] || die "$target: $f is not installed"
        if [[ -d $INSTALL/bin/plugins/$f ]]; then
            python3 -I -X pycache_prefix="$cache" -m compileall -q "$INSTALL/bin/plugins/$f" >/dev/null ||
                die "$target: $f does not compile"
        else
            python3 -I -X pycache_prefix="$cache" -m py_compile "$INSTALL/bin/plugins/$f" ||
                die "$target: $f does not compile"
        fi
    done
}
build_preview_dds() { python_plugin preview_dds DDSPreview.py; }
build_form43_checker() { python_plugin form43_checker Form43Checker.py; }
build_script_extender_plugin_checker() {
    python_plugin script_extender_plugin_checker ScriptExtenderPluginChecker.py
}
build_tool_configurator() { python_plugin tool_configurator pyCfg.py; }
build_installer_wizard() {
    python_plugin installer_wizard installer_wizard
    # the interpreter of the wizard scripts is installed by pip (plugin-requirements.txt)
    [[ $TESTS == 1 ]] || return 0
    [[ -d $INSTALL/bin/plugins/installer_wizard/lib/wizard ]] ||
        die "installer_wizard: the pip package bain-wizard-interpreter is not installed in lib/"
}
build_fnistool() { python_plugin fnistool FNISTool.py FNISPatches.py FNISToolReset.py; }
# basic_games (Phase 4d.2): the meta-plugin of the games that are not Bethesda's, with
# its pip dependencies; with --tests its pytest suite (tests/basic-games) imports the
# installed package with the installed mobase, in a HOME with a fake Steam
build_basic_games() {
    python_plugin basic_games basic_games
    [[ $TESTS == 1 ]] || return 0
    [[ -d $INSTALL/bin/plugins/basic_games/lib/vdf ]] ||
        die "basic_games: the pip package vdf is not installed in lib/"
    PYTHONPATH="$INSTALL/bin/plugins/plugin_python/libs:$INSTALL/bin/plugins" \
        python3 -m pytest -p no:cacheprovider "$ROOT/tests/basic-games" -q ||
        die "basic_games tests failed"
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
# after the last target: the smoke test needs the game plugin for its main window step
if [[ " ${failed[*]} " == *" modorganizer "* ]]; then
    log "smoke test skipped: modorganizer failed"
else
    smoke_modorganizer
fi
# skipped when a target it needs failed under --allow-fail
if [[ " ${failed[*]} " == *" mo2-vfs "* || " ${failed[*]} " == *" modorganizer "* ||
    " ${failed[*]} " == *" game_bethesda "* ]]; then
    log "vfs end-to-end test skipped: a target it needs failed"
else
    vfs_e2e
fi
if [[ " ${failed[*]} " == *" mo2-vfs "* || " ${failed[*]} " == *" libloot "* ||
    " ${failed[*]} " == *" lootcli "* ]]; then
    log "loot end-to-end test skipped: a target it needs failed"
else
    loot_e2e
fi
((${#skipped[@]})) && log "skipped (not implemented yet): ${skipped[*]}"
((${#failed[@]})) && log "failed (allowed): ${failed[*]}"
[[ ! -e $INSTALL/lib64 ]] || die "$INSTALL/lib64 exists: libraries must install into lib/"
log "done; install prefix: $INSTALL"
