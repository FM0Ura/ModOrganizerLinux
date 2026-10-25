#!/usr/bin/env bash
# Smoke test of the AppImage in a clean container of a distribution (spec of Phase 5,
# section 5.2):
#
#   tests/appimage/run-distro-smoke.sh <AppImage> <image>       # exit 0: passed
#   tests/appimage/run-distro-smoke.sh <AppImage> --all          # every image of distros.txt
#
# <image> is a line of distros.txt (its first field, e.g. fedora:44). In a fresh
# container of the image (podman, or docker if there is no podman) it
# - installs only the runtime packages of distros.txt (what the AppImage takes from the
#   host, see there);
# - extracts the AppImage (--appimage-extract: containers have no FUSE) and runs from
#   squashfs-root: `lootcli`, `mo2-vfs --list-mounts` and tests/smoke/modorganizer-smoke.sh with
#   MO2_SMOKE_APP=squashfs-root/AppRun, QT_QPA_PLATFORM=offscreen (--help, no display, two
#   starts, main window with a ready instance, nxm://, restart in the same process);
# - checks HTTPS from inside the AppImage on that distribution: (1) lootcli downloads the
#   LOOT masterlist (bundled libcurl, CA bundle of the host) and (2) Qt's networking
#   (QSslSocket, bundled OpenSSL) gets an answer from https://api.github.com, through
#   the AppDir's Python and PyQt6 (tests/appimage/pyrun.c, qt-https-check.py).
# QtWebEngine runs as root in the container, hence MO2's smoke adds --no-sandbox.
#
# MO2_CONTAINER_ENGINE (podman or docker) overrides the choice of the engine (CI uses docker).
#
# pyrun (the AppDir has libpython but no interpreter) is compiled once, in the build
# image (MO2_BUILD_IMAGE, localhost/mo2-linux-build:phase5 by default) or with the gcc
# of the host, into MO2_PYRUN_DIR (a temporary directory by default); MO2_PYRUN names a
# ready-made one instead.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
APPIMAGE="${1:?usage: $0 <AppImage> <image|--all>}"
IMAGE="${2:?usage: $0 <AppImage> <image|--all>}"

fail() { echo "distro smoke test FAILED: $*" >&2; exit 1; }

[[ -f $APPIMAGE ]] || fail "$APPIMAGE not found"
APPIMAGE="$(cd "$(dirname "$APPIMAGE")" && pwd)/$(basename "$APPIMAGE")"

if [[ -n ${MO2_CONTAINER_ENGINE:-} ]]; then CE=$MO2_CONTAINER_ENGINE
elif command -v podman >/dev/null; then CE=podman
elif command -v docker >/dev/null; then CE=docker
else fail "neither podman nor docker found"; fi

if [[ $IMAGE == --all ]]; then
    rc=0
    while IFS='|' read -r img _; do
        [[ -z $img || $img == \#* ]] && continue
        "$0" "$APPIMAGE" "$img" || rc=1
    done < "$HERE/distros.txt"
    exit "$rc"
fi

line="$(grep -E "^${IMAGE//./\\.}\|" "$HERE/distros.txt" || true)"
[[ -n $line ]] || fail "$IMAGE is not in $HERE/distros.txt"
IFS='|' read -r _ PM PKGS <<< "$line"
case $PM in
    dnf) INSTALL_CMD="dnf install -y --setopt=install_weak_deps=False $PKGS" ;;
    pacman) INSTALL_CMD="pacman -Syu --noconfirm --needed $PKGS" ;;
    apt) INSTALL_CMD="apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $PKGS" ;;
    *) fail "unknown package manager $PM" ;;
esac

# --- pyrun ---------------------------------------------------------------------------
PYRUN_DIR="${MO2_PYRUN_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/mo2-pyrun.XXXXXX")}"
mkdir -p "$PYRUN_DIR"
PYRUN="${MO2_PYRUN:-$PYRUN_DIR/pyrun}"
if [[ ! -x $PYRUN ]]; then
    if command -v gcc >/dev/null && pkg-config --exists python3-embed 2>/dev/null; then
        gcc -o "$PYRUN" "$HERE/pyrun.c" $(pkg-config --cflags --libs python3-embed)
    else
        "$CE" run --rm -v "$HERE:/src:ro,z" -v "$PYRUN_DIR:/out:z" \
            "${MO2_BUILD_IMAGE:-localhost/mo2-linux-build:phase5}" \
            sh -c 'gcc -o /out/pyrun /src/pyrun.c $(pkg-config --cflags --libs python3-embed)' ||
            fail "could not compile pyrun (set MO2_PYRUN or MO2_BUILD_IMAGE)"
    fi
fi
[[ -x $PYRUN ]] || fail "$PYRUN is not executable"

# --- the test inside the container ---------------------------------------------------
INNER='
set -euo pipefail
'"$INSTALL_CMD"' >/tmp/install.log 2>&1 || { cat /tmp/install.log; echo "package installation failed"; exit 1; }
mkdir -p /work && cd /work
cp /in/app.AppImage ./app.AppImage && chmod +x app.AppImage
./app.AppImage --appimage-extract >/dev/null
U=/work/squashfs-root/usr
echo "=== $(. /etc/os-release; echo "$PRETTY_NAME"), glibc $(ldd --version | head -n1 | grep -oE "[0-9.]+$")"

echo "--- lootcli and mo2-vfs"
# lootcli has no --help: without arguments it says which one is missing, and exits 1
out="$("$U/bin/loot/lootcli" --help 2>&1 || true)"
[[ $out == *"argument missing game"* ]] || { echo "lootcli did not run: $out"; exit 1; }
"$U/bin/mo2-vfs" --list-mounts >/dev/null || { echo "mo2-vfs did not run"; exit 1; }
echo "lootcli and mo2-vfs run"

echo "--- smoke test of the AppImage"
export HOME=/work/home XDG_CACHE_HOME=/work/home/.cache XDG_DATA_HOME=/work/home/.local/share
mkdir -p "$HOME" /work/tmp
CI=1 MO2_SMOKE_STRICT_HOME=1 MO2_SMOKE_APP=/work/squashfs-root/AppRun QT_QPA_PLATFORM=offscreen \
    TMPDIR=/work/tmp /in/repo/tests/smoke/modorganizer-smoke.sh "$U"

echo "--- HTTPS 1: lootcli downloads the masterlist (bundled libcurl)"
d=/work/loot; mkdir -p $d/game/Data $d/prof $d/xdg; : > $d/game/SkyrimSE.exe; : > $d/game/Data/Skyrim.esm; : > $d/prof/loadorder.txt
out="$(XDG_DATA_HOME=$d/xdg "$U/bin/loot/lootcli" --game SkyrimSE --gamePath $d/game \
    --pluginListPath $d/prof/loadorder.txt --out $d/out.json --language en 2>&1 || true)"
printf "%s\n" "$out" | head -n 12
ml="$d/xdg/LOOT/games/Skyrim Special Edition/masterlist.yaml"
[[ -s $ml && $(stat -c %s "$ml") -gt 10000 ]] || { echo "the masterlist was not downloaded"; exit 1; }
grep -qiE "\[error\].*(download|curl|certificate|SSL)" <<< "$out" && { echo "download error"; exit 1; }
echo "lootcli https ok ($(stat -c %s "$ml") bytes)"

echo "--- HTTPS 2: Qt networking with the bundled OpenSSL"
env -i PATH="$PATH" HOME="$HOME" LD_LIBRARY_PATH="$U/lib" PYTHONHOME="$U" \
    PYTHONPATH="$U/lib/python3.14/site-packages" QT_PLUGIN_PATH="$U/plugins" \
    QT_QPA_PLATFORM=offscreen /in/pyrun /in/qt-https-check.py https://api.github.com

echo "DISTRO SMOKE PASSED"
'
echo "##### $IMAGE"
"$CE" run --rm --pull=missing \
    -v "$APPIMAGE:/in/app.AppImage:ro,z" -v "$ROOT:/in/repo:ro,z" \
    -v "$PYRUN:/in/pyrun:ro,z" -v "$HERE/qt-https-check.py:/in/qt-https-check.py:ro,z" \
    "$IMAGE" bash -c "$INNER" || fail "$IMAGE"
echo "distro smoke test passed: $IMAGE"
