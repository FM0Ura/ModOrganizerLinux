#!/usr/bin/env bash
# End-to-end test of the update of the AppImage by Mod Organizer (spec of Phase 5,
# section 3.4), headless and without GitHub:
#
#   tests/selfupdate-e2e/run.sh [<install prefix>]     (install/ by default)
#
# A local HTTP server (python3 -m http.server, on 127.0.0.1) plays the GitHub API and
# the download of the release assets; MO2_UPDATE_API_URL (development only, github.cpp)
# points Mod Organizer at it. The installed ModOrganizer runs as if it were an AppImage:
# APPIMAGE is a file in a folder with spaces and accents, its "new version"
# (ModOrganizer-2.5.2.2-x86_64.AppImage on the server) a shell script that writes its
# arguments and part of its environment to a file and exits. MO2_SMOKE_ACCEPT_UPDATE=1
# (tests only, selfupdater.cpp) accepts the update dialog without showing it; the GUI
# steps are in docs/testing/phase5-manual-checklist.md.
#
# 1. update: Mod Organizer (2.5.2 locally, or the MO2_VERSION of the build) sees v2.5.2.2
#    (the draft v2.5.2.3 is ignored), downloads SHA256SUMS and then the AppImage next to
#    APPIMAGE, replaces APPIMAGE (keeping its 0750 mode), exits normally and executes the
#    new APPIMAGE with the same arguments (--logs -i smoke) and without APPDIR. No
#    .update-* file may be left.
# 2. wrong checksum: the same with a SHA256SUMS that does not match; the old APPIMAGE must
#    stay as it was, the download must be removed and Mod Organizer must not restart (it
#    shows the error and keeps running; the test then stops it).
# 3. no releases (the 404 of a private repository): only logged, nothing downloaded, Mod
#    Organizer keeps running.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
INSTALL="${1:-$ROOT/install}"
APP="$INSTALL/bin/ModOrganizer"
here="$ROOT/tests/selfupdate-e2e"

fail() { echo "self-update e2e test FAILED: $*" >&2; exit 1; }

[[ -x $APP ]] || fail "$APP not found"
command -v python3 > /dev/null || fail "python3 not found"

work="$(mktemp -d "${TMPDIR:-/tmp}/mo2-selfupdate.XXXXXX")"
pid=""
server=""
cleanup() {
    [[ -z $pid ]] || kill -KILL "$pid" 2>/dev/null || true
    [[ -z $server ]] || kill "$server" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT

# a free TCP port of 127.0.0.1
port="$(python3 -I -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')"
base="http://127.0.0.1:$port"
srv="$work/server"
mkdir -p "$srv/repos/FM0Ura/ModOrganizerLinux" "$srv/download"
python3 -I -m http.server --bind 127.0.0.1 --directory "$srv" "$port" \
    > "$work/server.log" 2>&1 &
server=$!

# the new version: records how it was started
new="$srv/download/ModOrganizer-2.5.2.2-x86_64.AppImage"
cat > "$new" <<'EOF'
#!/bin/sh
{
    printf 'argv0=%s\n' "$0"
    for a in "$@"; do printf 'arg=%s\n' "$a"; done
    printf 'APPDIR=%s\n' "${APPDIR-<unset>}"
} > "$MO2_E2E_MARKER"
EOF
chmod 0644 "$new"

# release_json <tag> <draft>
release_json() {
    cat <<EOF
{
  "html_url": "https://github.com/FM0Ura/ModOrganizerLinux/releases/tag/$1",
  "tag_name": "$1",
  "name": "Mod Organizer ${1#v} for Linux",
  "draft": $2,
  "prerelease": false,
  "body": "test release $1",
  "assets": [
    {"name": "ModOrganizer-${1#v}-x86_64.AppImage.zsync",
     "content_type": "application/octet-stream",
     "browser_download_url": "$base/download/ModOrganizer-${1#v}-x86_64.AppImage.zsync"},
    {"name": "ModOrganizer-${1#v}-x86_64.AppImage",
     "content_type": "application/octet-stream",
     "browser_download_url": "$base/download/ModOrganizer-${1#v}-x86_64.AppImage"},
    {"name": "SHA256SUMS", "content_type": "text/plain",
     "browser_download_url": "$base/download/SHA256SUMS"}
  ]
}
EOF
}
{
    echo "["
    release_json v2.5.2.3 true
    echo ","
    release_json v2.5.2.2 false
    echo "]"
} > "$srv/repos/FM0Ura/ModOrganizerLinux/releases"

for ((i = 0; i < 50; i++)); do
    python3 -I -c "import urllib.request; urllib.request.urlopen('$base/download/')" \
        2>/dev/null && break
    sleep 0.2
done

# run_mo2 <name> <sums contents> [<API URL>]: sets up a fresh HOME, instance and APPIMAGE
# and starts Mod Organizer in the background (pid)
run_mo2() {
    local name="$1" sums="$2" api="${3:-$base}" home game
    case_dir="$work/$name"
    home="$case_dir/home"
    instance="$home/.local/share/ModOrganizer/smoke"
    game="$case_dir/game"
    out="$case_dir/app.out"
    marker="$case_dir/marker"
    apps="$case_dir/Meus Programas ç"
    appimage="$apps/Mod Organizer.AppImage"
    mkdir -p "$home"/{.config,.cache,.local/share,.local/state} "$case_dir/tmp" "$apps"
    mkdir -m 700 "$case_dir/runtime"
    # shellcheck source=../lib/instance.sh
    source "$ROOT/tests/lib/instance.sh"
    mo2_make_instance "$ROOT/tests/fixtures/vfs-e2e" "$case_dir" "$game" "$instance" /bin/true
    sed -i -e 's|^\[General\]$|&\nfirst_start=false\nversion=2.5.2|' \
        -e 's|^\[Settings\]$|&\nlog_level=0|' \
        -e 's|^check_for_updates=false$|check_for_updates=true|' "$instance/ModOrganizer.ini"
    grep -qx "check_for_updates=true" "$instance/ModOrganizer.ini" ||
        fail "could not enable check_for_updates in the instance"
    printf 'the old version\n' > "$appimage"
    chmod 0750 "$appimage"
    printf '%s' "$sums" > "$srv/download/SHA256SUMS"

    env HOME="$home" \
        XDG_CONFIG_HOME="$home/.config" XDG_DATA_HOME="$home/.local/share" \
        XDG_CACHE_HOME="$home/.cache" XDG_STATE_HOME="$home/.local/state" \
        XDG_RUNTIME_DIR="$case_dir/runtime" TMPDIR="$case_dir/tmp" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=$case_dir/no-bus" \
        MO2_VFS_BRIDGE=direct QT_QPA_PLATFORM=offscreen \
        QTWEBENGINE_CHROMIUM_FLAGS="--no-sandbox --disable-gpu" \
        APPIMAGE="$appimage" APPDIR="$INSTALL" \
        MO2_UPDATE_API_URL="$api" MO2_SMOKE_ACCEPT_UPDATE=1 MO2_E2E_MARKER="$marker" \
        "$APP" --logs -i smoke > "$out" 2>&1 < /dev/null &
    pid=$!
}

leftovers() {
    find "$apps" -name '.*.update-*' | head -n 5
}

echo "--- update to v2.5.2.2"
sha="$(sha256sum "$new" | cut -d' ' -f1)"
run_mo2 update "$sha  ModOrganizer-2.5.2.2-x86_64.AppImage
0000000000000000000000000000000000000000000000000000000000000000  ModOrganizer-2.5.2.2-x86_64.AppImage.zsync
"
rc=0
for ((i = 0; i < 120; i++)); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.5
done
if kill -0 "$pid" 2>/dev/null; then
    cat "$out"
    fail "Mod Organizer did not exit within 60 s"
fi
wait "$pid" || rc=$?
pid=""
cat "$out"
grep -E "update: |update available|no releases" "$instance/logs/mo_interface.log" || true
((rc == 0)) || fail "exit code $rc (the new version exits with 0)"
[[ -f $marker ]] || fail "the new AppImage was not executed"
echo "--- the new version was started with:"
cat "$marker"
expected="argv0=$appimage
arg=--logs
arg=-i
arg=smoke
APPDIR=<unset>"
[[ $(<"$marker") == "$expected" ]] || fail "unexpected arguments or environment, expected:
$expected"
cmp -s "$new" "$appimage" || fail "APPIMAGE was not replaced by the new version"
[[ $(stat -c %a "$appimage") == 750 ]] ||
    fail "APPIMAGE has mode $(stat -c %a "$appimage"), expected 750"
[[ -z $(leftovers) ]] || fail "download left behind: $(leftovers)"
grep -q "update available: 2.5.2[.0-9]* -> 2.5.2.2" "$instance/logs/mo_interface.log" ||
    fail "no 'update available ... -> 2.5.2.2' in the log"
echo "update test passed"

echo "--- wrong checksum"
run_mo2 mismatch "$(printf '%064d' 1)  ModOrganizer-2.5.2.2-x86_64.AppImage
"
found=0
for ((i = 0; i < 120; i++)); do
    if grep -q "can't replace the AppImage" "$instance/logs/mo_interface.log" 2>/dev/null; then
        found=1
        break
    fi
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.5
done
# the error is shown in a message box; give a wrong restart the time to happen
sleep 2
alive=0
kill -0 "$pid" 2>/dev/null && alive=1
kill -TERM "$pid" 2>/dev/null || true
for ((i = 0; i < 20; i++)); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.5
done
kill -KILL "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
pid=""
grep -E "update: |update available|no releases" "$instance/logs/mo_interface.log" || true
((found)) || { cat "$out"; fail "no \"can't replace the AppImage\" in the log"; }
((alive)) || fail "Mod Organizer exited after the failed update"
[[ ! -e $marker ]] || fail "the new AppImage was executed after a checksum mismatch"
[[ $(<"$appimage") == "the old version" ]] || fail "APPIMAGE was changed"
[[ $(stat -c %a "$appimage") == 750 ]] || fail "the mode of APPIMAGE was changed"
[[ -z $(leftovers) ]] || fail "download left behind: $(leftovers)"
echo "wrong checksum test passed"

echo "--- no releases (HTTP 404)"
run_mo2 notfound "" "$base/no-such-api"
found=0
for ((i = 0; i < 120; i++)); do
    if grep -q "no releases found" "$instance/logs/mo_interface.log" 2>/dev/null; then
        found=1
        break
    fi
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.5
done
sleep 1
alive=0
kill -0 "$pid" 2>/dev/null && alive=1
kill -TERM "$pid" 2>/dev/null || true
for ((i = 0; i < 20; i++)); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.5
done
kill -KILL "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
pid=""
grep -E "update: |update available|no releases" "$instance/logs/mo_interface.log" || true
((found)) || { cat "$out"; fail "no \"no releases found\" in the log"; }
((alive)) || fail "Mod Organizer exited after the 404"
! grep -q "update: downloading" "$instance/logs/mo_interface.log" ||
    fail "something was downloaded without a release"
[[ $(<"$appimage") == "the old version" ]] || fail "APPIMAGE was changed"
echo "no releases test passed"

echo "self-update e2e test passed"
