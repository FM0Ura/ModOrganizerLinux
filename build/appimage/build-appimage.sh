#!/usr/bin/env bash
# Build the Mod Organizer AppImage from the install tree of ./build.sh (spec of Phase 5,
# sections 3.1, 3.2, 3.5 and 3.7). Runs in the mo2-linux-build container (Fedora 44,
# build/Containerfile): the Qt, Python and PyQt6 of the AppImage are the container's,
# the ones the install tree was built against.
#
#   MO2_VERSION=2.5.2.1 build/appimage/build-appimage.sh <install-dir> <out-dir>
#
# Environment:
#   MO2_VERSION  version in the file names (required), e.g. 2.5.2.1
#   BUILD_OUT    cache of the downloaded tools (default <root>/build-out), in
#                $BUILD_OUT/appimage-tools
#   QMAKE        qmake of the Qt to bundle (default qmake6)
#
# Produces in <out-dir>:
#   ModOrganizer-<version>-x86_64.AppImage         type 2 runtime (static, no libfuse2)
#   ModOrganizer-<version>-x86_64.AppImage.zsync   update information: see UPDATE_INFO
#   ModOrganizer-<version>-x86_64.debug.tar.zst    .build-id/xx/yyyy.debug of every ELF
#                                                  that had debug information
#   SHA256SUMS                                     of the three files above
#   AppDir/                                        what the AppImage contains (kept for
#                                                  the smoke tests)
#
# The AppDir keeps the layout of the install tree under usr/ (bin/ModOrganizer finds
# its plugins and resources next to itself): usr/bin/{ModOrganizer,mo2-vfs,loot/,
# plugins/,...}, usr/lib/{libuibase.so,...,Qt,libstdc++.so.6,libgcc_s.so.1},
# usr/lib/python3.14 (the standard library, PyQt6 in its site-packages) with
# usr/lib/libpython3.14.so.1.0, usr/plugins (Qt), usr/libexec/QtWebEngineProcess and
# usr/share/licenses/ModOrganizer. Every ELF finds its libraries through an
# $ORIGIN-relative RUNPATH, without any environment (scripts/check-appdir.sh, run
# before the AppImage is made, checks it).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BUILD_OUT="${BUILD_OUT:-$ROOT/build-out}"
QMAKE="${QMAKE:-qmake6}"

# --- pinned tools (version and SHA-256, also in deps/README.md) -------------
LINUXDEPLOY_VERSION="1-alpha-20251107-1"
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_VERSION/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"

LINUXDEPLOY_QT_VERSION="1-alpha-20250213-1"
LINUXDEPLOY_QT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$LINUXDEPLOY_QT_VERSION/linuxdeploy-plugin-qt-x86_64.AppImage"
LINUXDEPLOY_QT_SHA256="15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724"

APPIMAGETOOL_VERSION="1.9.1"
APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VERSION/appimagetool-x86_64.AppImage"
APPIMAGETOOL_SHA256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"

RUNTIME_VERSION="20251108"
RUNTIME_URL="https://github.com/AppImage/type2-runtime/releases/download/$RUNTIME_VERSION/runtime-x86_64"
RUNTIME_SHA256="2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"

UPDATE_INFO="gh-releases-zsync|FM0Ura|ModOrganizerLinux|latest|ModOrganizer-*-x86_64.AppImage.zsync"

PYTHON_VERSION="3.14"
# the PyQt6 modules bundled: the ones of the Qt libraries the AppImage has anyway
# (the plugins of Mod Organizer use QtCore, QtGui, QtWidgets and QtOpenGL*); the others
# (Multimedia, Bluetooth, Quick3D, Pdf, ...) would bring their Qt libraries in
PYQT6_MODULES=(
    QtCore QtGui QtWidgets QtNetwork QtDBus QtOpenGL QtOpenGLWidgets QtPrintSupport
    QtQml QtQuick QtQuickWidgets QtPositioning QtWebChannel QtWebSockets
    QtSvg QtSvgWidgets QtXml QtTest
)
# libraries the exclude list of linuxdeploy leaves to the host, which the AppImage
# bundles anyway: the host provides only what scripts/check-appdir.sh allows (glibc,
# graphics, display, fontconfig, freetype, D-Bus), and these are not always installed
# (libz, libasound, libpipewire, ...) or must match the bundled libraries
BUNDLE_ANYWAY=(
    libz.so.1 libexpat.so.1 libuuid.so.1 libgmp.so.10 libgpg-error.so.0 libcom_err.so.2
    libharfbuzz.so.0 libfribidi.so.0 libSM.so.6 libICE.so.6 libasound.so.2
    libpipewire-0.3.so.0
)
# libraries that nothing links but that a bundled library opens with dlopen from its own
# directory: the modules of NSS (QtWebEngine), libnssckbi.so being p11-kit's trust module
DLOPEN_MODULES=(
    libsoftokn3.so libsoftokn3.chk libfreeblpriv3.so libfreeblpriv3.chk
    libfreebl3.so libfreebl3.chk libnssckbi.so
)
# packages compiled into the binaries (headers only or static), whose licence files
# are bundled besides those of the packages that own a bundled file
STATIC_PACKAGES=(boost-devel fmt-devel spdlog-devel gsl-lite-devel tomlplusplus-devel lz4-devel)

log() { echo "==> appimage: $*"; }
die() { echo "build-appimage.sh: $*" >&2; exit 1; }

(($# == 2)) || die "usage: MO2_VERSION=<version> $0 <install-dir> <out-dir>"
[[ -n ${MO2_VERSION:-} ]] || die "MO2_VERSION is not set (e.g. MO2_VERSION=2.5.2.1)"
[[ $MO2_VERSION =~ ^[0-9A-Za-z.+~-]+$ ]] || die "unexpected MO2_VERSION '$MO2_VERSION'"
INSTALL="$(cd "$1" && pwd)" || die "no install tree at $1"
[[ -x $INSTALL/bin/ModOrganizer ]] || die "$INSTALL/bin/ModOrganizer not found"
mkdir -p "$2"
OUT="$(cd "$2" && pwd)"

NAME="ModOrganizer-$MO2_VERSION-x86_64"
APPDIR="$OUT/AppDir"
WORK="$OUT/.work"
TOOLS="$BUILD_OUT/appimage-tools"

command -v "$QMAKE" >/dev/null || die "$QMAKE not found"
for tool in patchelf objcopy strip readelf zsyncmake zstd desktop-file-validate rpm; do
    command -v "$tool" >/dev/null || die "$tool not found (see build/Containerfile)"
done

# --- tools (cached in build-out/appimage-tools, verified by SHA-256) ----------
# fetch <file> <url> <sha256>: the verified download in $TOOLS/<file>
fetch() {
    local file="$TOOLS/$1" url="$2" sha="$3"
    if [[ ! -f $file ]] || ! echo "$sha  $file" | sha256sum --quiet -c - 2>/dev/null; then
        log "downloading $url"
        mkdir -p "$TOOLS"
        curl -fsSL --retry 3 -o "$file.part" "$url"
        echo "$sha  $file.part" | sha256sum --quiet -c - ||
            die "SHA-256 of $url is $(sha256sum "$file.part" | cut -d' ' -f1), expected $sha"
        mv "$file.part" "$file"
    fi
    chmod +x "$file"
}

# extracted <name> <AppImage>: extracts an AppImage once into $TOOLS/<name> (no FUSE
# needed, e.g. in a container) and prints the path of its AppRun
extracted() {
    local dir="$TOOLS/$1" image="$2"
    if [[ ! -x $dir/squashfs-root/AppRun || $dir/.from -ot $image ]]; then
        rm -rf "$dir"
        mkdir -p "$dir"
        (cd "$dir" && "$image" --appimage-extract >/dev/null) ||
            die "could not extract $image"
        touch "$dir/.from"
    fi
    echo "$dir/squashfs-root/AppRun"
}

fetch "linuxdeploy-$LINUXDEPLOY_VERSION.AppImage" "$LINUXDEPLOY_URL" "$LINUXDEPLOY_SHA256"
fetch "linuxdeploy-plugin-qt-$LINUXDEPLOY_QT_VERSION.AppImage" "$LINUXDEPLOY_QT_URL" "$LINUXDEPLOY_QT_SHA256"
fetch "appimagetool-$APPIMAGETOOL_VERSION.AppImage" "$APPIMAGETOOL_URL" "$APPIMAGETOOL_SHA256"
fetch "runtime-x86_64-$RUNTIME_VERSION" "$RUNTIME_URL" "$RUNTIME_SHA256"
chmod -x "$TOOLS/runtime-x86_64-$RUNTIME_VERSION"

LINUXDEPLOY="$(extracted "linuxdeploy-$LINUXDEPLOY_VERSION" "$TOOLS/linuxdeploy-$LINUXDEPLOY_VERSION.AppImage")"
LINUXDEPLOY_QT="$(extracted "linuxdeploy-plugin-qt-$LINUXDEPLOY_QT_VERSION" "$TOOLS/linuxdeploy-plugin-qt-$LINUXDEPLOY_QT_VERSION.AppImage")"
APPIMAGETOOL="$(extracted "appimagetool-$APPIMAGETOOL_VERSION" "$TOOLS/appimagetool-$APPIMAGETOOL_VERSION.AppImage")"

# linuxdeploy finds its plugins through PATH, by name
mkdir -p "$TOOLS/bin"
ln -sfn "$LINUXDEPLOY_QT" "$TOOLS/bin/linuxdeploy-plugin-qt"

# --- helpers ----------------------------------------------------------------
is_elf() { [[ -f $1 && ! -L $1 && $(head -c 4 "$1" | od -An -c | tr -d ' ') == '177ELF' ]]; }

# every ELF file of the AppDir, one per line
appdir_elfs() {
    find "$APPDIR" -type f -print0 | while IFS= read -r -d '' f; do
        is_elf "$f" && echo "$f"
    done
}

# fix_runpaths: the RUNPATH of every ELF of the AppDir gets $ORIGIN-relative entries
# only, one of them reaching usr/lib (the install tree has absolute entries, e.g. the
# Python proxy's, and the plugins of Mod Organizer and Python have none: they relied on
# the libraries already loaded)
fix_runpaths() {
    local f old rel want new e
    local -a entries parts
    while IFS= read -r f; do
        [[ $(readelf -d "$f" 2>/dev/null) == *'(NEEDED)'* ]] || continue
        old="$(patchelf --print-rpath "$f")"
        rel="$(realpath --relative-to="$(dirname "$f")" "$APPDIR/usr/lib")"
        want='$ORIGIN'
        [[ $rel == . ]] || want="\$ORIGIN/$rel"

        entries=()
        IFS=: read -r -a parts <<<"$old"
        for e in "${parts[@]}"; do
            [[ $e == '$ORIGIN'* || $e == '${ORIGIN}'* ]] && entries+=("$e")
        done
        [[ " ${entries[*]} " == *" $want "* ]] || entries+=("$want")
        new="$(IFS=:; echo "${entries[*]}")"

        if [[ $new != "$old" ]]; then
            chmod u+w "$f"
            patchelf --set-rpath "$new" "$f"
        fi
    done < <(appdir_elfs)
}

# --- AppDir: the install tree -------------------------------------------------
log "AppDir in $APPDIR"
rm -rf "$APPDIR" "$WORK"
mkdir -p "$APPDIR/usr/lib" "$WORK"
cp -a "$INSTALL/bin" "$APPDIR/usr/"
# the shared libraries only (no static libraries, headers or CMake packages)
find "$INSTALL/lib" -maxdepth 1 \( -name '*.so' -o -name '*.so.*' \) \
    -exec cp -a {} "$APPDIR/usr/lib/" \;

# --- debug symbols ------------------------------------------------------------------
# for each ELF with debug information (those of the install tree: the Fedora libraries
# have none): the debug information goes to debug/.build-id/xx/yyyy.debug (where gdb
# looks with set debug-file-directory), the ELF keeps its symbol table and a
# .gnu_debuglink to it. Before any patchelf: strip and objcopy break some ELFs whose
# dynamic sections patchelf moved (mo2-vfs)
log "debug symbols"
DEBUG="$WORK/debug"
mkdir -p "$DEBUG/.build-id"
while IFS= read -r f; do
    [[ $(readelf -S "$f" 2>/dev/null) == *'.debug_info'* ]] || continue
    id="$(LC_ALL=C readelf -n "$f" 2>/dev/null | sed -n 's/^ *Build ID: *//p' | head -n 1)"
    [[ $id =~ ^[0-9a-f]{3,}$ ]] || die "${f#"$APPDIR"/} has debug information but no build ID"
    dir="$DEBUG/.build-id/${id:0:2}"
    dbg="$dir/${id:2}.debug"
    mkdir -p "$dir"
    chmod u+w "$f"
    objcopy --only-keep-debug "$f" "$dbg"
    strip --strip-debug "$f"
    objcopy --add-gnu-debuglink="$dbg" "$f"
done < <(appdir_elfs)

# --- Python -------------------------------------------------------------------
PYLIB="/usr/lib64/python$PYTHON_VERSION"
[[ -f $PYLIB/os.py ]] || die "no Python $PYTHON_VERSION standard library in $PYLIB"
log "Python $PYTHON_VERSION from $PYLIB"
cp -L "/usr/lib64/libpython$PYTHON_VERSION.so.1.0" "$APPDIR/usr/lib/"
tar -C "$(dirname "$PYLIB")" \
    --exclude="python$PYTHON_VERSION/site-packages" \
    --exclude="python$PYTHON_VERSION/test" \
    --exclude="python$PYTHON_VERSION/idlelib" \
    --exclude="python$PYTHON_VERSION/tkinter" \
    --exclude="python$PYTHON_VERSION/turtledemo" \
    --exclude="python$PYTHON_VERSION/ensurepip" \
    --exclude="python$PYTHON_VERSION/lib-dynload/_tkinter.*" \
    -cf - "python$PYTHON_VERSION" | tar -C "$APPDIR/usr/lib" -xf -

PYQT6="$(python3 -I -c 'import os, PyQt6; print(os.path.dirname(PyQt6.__file__))')"
SITE="$APPDIR/usr/lib/python$PYTHON_VERSION/site-packages"
log "PyQt6 from $PYQT6"
mkdir -p "$SITE"
cp -a "$PYQT6" "$SITE/"
rm -rf "$SITE/PyQt6/bindings"
for so in "$SITE"/PyQt6/Qt*.abi3.so; do
    module="$(basename "$so" .abi3.so)"
    if [[ " ${PYQT6_MODULES[*]} " != *" $module "* ]]; then
        rm -f "$so" "$SITE/PyQt6/$module.pyi"
    fi
done
for module in "${PYQT6_MODULES[@]}"; do
    [[ -f $SITE/PyQt6/$module.abi3.so ]] || die "PyQt6.$module not found in $PYQT6"
done

# the compiled files that the runner (optimization level 2) reads, no others
find "$APPDIR/usr/lib/python$PYTHON_VERSION" -path '*/__pycache__/*' ! -name '*.opt-2.pyc' -delete

# the platform library directory of Fedora's Python is lib64: with the home of the
# runner (usr), the standard library is looked for in usr/lib64/python3.14
mkdir -p "$APPDIR/usr/lib64"
ln -s "../lib/python$PYTHON_VERSION" "$APPDIR/usr/lib64/python$PYTHON_VERSION"

# --- linuxdeploy: dependencies, Qt plugins, QtWebEngine ---------------------
# (linuxdeploy resolves the dependencies through the RUNPATHs, inside the AppDir)
fix_runpaths

# desktop file and icon (the 256 px PNG of the Windows icon)
cp "$HERE/ModOrganizer.desktop" "$WORK/ModOrganizer.desktop"
desktop-file-validate "$WORK/ModOrganizer.desktop"
python3 -I - "$ROOT/deps/modorganizer/src/mo_icon.ico" "$WORK/ModOrganizer.png" <<'EOF'
import struct, sys
data = open(sys.argv[1], "rb").read()
count = struct.unpack("<HHH", data[:6])[2]
for i in range(count):
    w, h, _, _, _, _, size, offset = struct.unpack("<BBBBHHII", data[6 + 16 * i:22 + 16 * i])
    if w == 0 and h == 0 and data[offset:offset + 4] == b"\x89PNG":
        open(sys.argv[2], "wb").write(data[offset:offset + size])
        break
else:
    sys.exit("no 256 px PNG in " + sys.argv[1])
EOF

deps_args=()
while IFS= read -r f; do
    deps_args+=(--deploy-deps-only "$f")
done < <(appdir_elfs)
for lib in "${BUNDLE_ANYWAY[@]}"; do
    [[ -e /usr/lib64/$lib ]] || die "/usr/lib64/$lib not found"
    deps_args+=(--library "/usr/lib64/$lib")
done
# (the Qt plugin links the translations it finds into usr/translations, which must exist)
mkdir -p "$APPDIR/usr/translations"

log "linuxdeploy (Qt plugin, ${#deps_args[@]} arguments)"
# PATCHELF: the patchelf of the container (0.18); the one inside linuxdeploy (0.15)
# breaks libraries of Fedora 44 (libcrypt.so.2 crashed when loaded); NO_STRIP: the debug
# information is split above; QML_SOURCES_PATHS: the tutorials import QtQuick; the
# platform plugins are xcb (default), Wayland and offscreen
env PATH="$TOOLS/bin:$PATH" \
    PATCHELF="$(command -v patchelf)" \
    QMAKE="$(command -v "$QMAKE")" \
    NO_STRIP=1 \
    DISABLE_COPYRIGHT_FILES_DEPLOYMENT=1 \
    EXTRA_QT_MODULES="waylandcompositor;svg" \
    EXTRA_PLATFORM_PLUGINS="libqwayland.so;libqoffscreen.so" \
    QML_SOURCES_PATHS="$APPDIR/usr/bin/tutorials" \
    "$LINUXDEPLOY" --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/ModOrganizer" \
    "${deps_args[@]}" \
    --custom-apprun "$HERE/AppRun" \
    --desktop-file "$WORK/ModOrganizer.desktop" \
    --icon-file "$WORK/ModOrganizer.png" \
    --plugin qt

[[ -x $APPDIR/usr/libexec/QtWebEngineProcess ]] || die "QtWebEngineProcess was not deployed"
[[ -d $APPDIR/usr/plugins/platforms ]] || die "the Qt platform plugins were not deployed"

# --- libstdc++ and libgcc_s (AppRun prefers the host's when newer), dlopen modules --
for lib in libstdc++.so.6 libgcc_s.so.1 "${DLOPEN_MODULES[@]}"; do
    [[ -e /usr/lib64/$lib ]] || die "/usr/lib64/$lib not found"
    cp -L "/usr/lib64/$lib" "$APPDIR/usr/lib/$lib"
done

# --- RUNPATH, again: the libraries linuxdeploy and this script added --------------
log "RUNPATH of every ELF"
fix_runpaths

# --- licences ------------------------------------------------------------------------
# the notices of the port (build/appimage/licenses), the licence texts of the sources
# built into the AppImage, and those of every Fedora package that owns a bundled file
log "licences"
LICENSES="$APPDIR/usr/share/licenses/ModOrganizer"
mkdir -p "$LICENSES"
cp -a "$HERE/licenses/." "$LICENSES/"
cp "$ROOT/deps/modorganizer/LICENSE" "$LICENSES/ModOrganizer-LICENSE.txt"
cp "$ROOT/deps/dds-header/LICENSE" "$LICENSES/DirectXTex-LICENSE.txt"
cp -a "$INSTALL/share/licenses/7zip" "$LICENSES/7zip"
for src in libloot libbsarchpp; do
    if [[ -f $BUILD_OUT/$src/src/LICENSE ]]; then
        cp "$BUILD_OUT/$src/src/LICENSE" "$LICENSES/$src-LICENSE.txt"
    else
        die "$BUILD_OUT/$src/src/LICENSE not found (build $src with ./build.sh first)"
    fi
done

# the packages: the owners of the files the AppDir took from the system (libraries,
# Qt plugins and programs, Python and PyQt6)
QT_PLUGINS="$($QMAKE -query QT_INSTALL_PLUGINS)"
QT_LIBEXECS="$($QMAKE -query QT_INSTALL_LIBEXECS)"
{
    find "$APPDIR/usr/lib" -maxdepth 1 -type f -printf '/usr/lib64/%f\n'
    find "$APPDIR/usr/plugins" -type f -printf "$QT_PLUGINS/%P\n"
    find "$APPDIR/usr/libexec" -type f -printf "$QT_LIBEXECS/%f\n"
    find "$APPDIR/usr/lib/python$PYTHON_VERSION" -path "$SITE" -prune -o -type f -printf "$PYLIB/%P\n"
    find "$SITE/PyQt6" -type f -printf "$PYQT6/%P\n"
} | while IFS= read -r path; do
    if [[ -e $path ]]; then echo "$path"; fi
done | { xargs -r -d '\n' rpm -qf --qf '%{NAME}\n' 2>/dev/null || true; } |
    { grep -v ' ' || true; } | sort -u >"$WORK/owners.txt"
printf '%s\n' "${STATIC_PACKAGES[@]}" >>"$WORK/owners.txt"

{
    echo "# Fedora packages whose files are in this AppImage (name-version-release: License)"
    echo
    sort -u "$WORK/owners.txt" | while IFS= read -r pkg; do
        rpm -q --qf '%{NAME}-%{VERSION}-%{RELEASE}: %{LICENSE}\n' "$pkg"
    done
} >"$LICENSES/fedora-packages.txt"
sort -u "$WORK/owners.txt" | while IFS= read -r pkg; do
    rpm -qL "$pkg" 2>/dev/null | while IFS= read -r file; do
        [[ -f $file ]] || continue
        mkdir -p "$LICENSES/fedora/$pkg"
        cp "$file" "$LICENSES/fedora/$pkg/"
    done
done

# --- checks -----------------------------------------------------------------------
MAX_GLIBC="$(ldd --version | head -n 1 | grep -oE '[0-9]+\.[0-9]+$')"
log "check-appdir (glibc $MAX_GLIBC)"
"$ROOT/scripts/check-appdir.sh" "$APPDIR" "$MAX_GLIBC"

# --- AppImage, zsync, symbols, checksums --------------------------------------------
log "appimagetool"
rm -f "$OUT/$NAME.AppImage" "$OUT/$NAME.AppImage.zsync" "$OUT/$NAME.debug.tar.zst" "$OUT/SHA256SUMS"
(
    cd "$OUT"
    ARCH=x86_64 "$APPIMAGETOOL" --no-appstream \
        --runtime-file "$TOOLS/runtime-x86_64-$RUNTIME_VERSION" \
        -u "$UPDATE_INFO" \
        "$APPDIR" "$OUT/$NAME.AppImage"
)
[[ -f $OUT/$NAME.AppImage ]] || die "appimagetool did not produce $NAME.AppImage"
[[ -f $OUT/$NAME.AppImage.zsync ]] || die "appimagetool did not produce $NAME.AppImage.zsync"

log "debug symbols archive"
tar -C "$DEBUG" --owner=0 --group=0 --sort=name -cf - .build-id |
    zstd -q -T0 -19 -o "$OUT/$NAME.debug.tar.zst"

(cd "$OUT" && sha256sum "$NAME.AppImage" "$NAME.AppImage.zsync" "$NAME.debug.tar.zst" >SHA256SUMS)
rm -rf "$WORK"

log "done: $(du -h "$OUT/$NAME.AppImage" | cut -f1) $OUT/$NAME.AppImage"
