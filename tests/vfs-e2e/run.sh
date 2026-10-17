#!/usr/bin/env bash
# End-to-end test of the virtual file system: the installed application runs a native
# Linux program of an instance with the mo2-vfs view of the game's data directory.
#
#   tests/vfs-e2e/run.sh <install prefix>
#
# In a temporary directory (everything the test and the application write stays in
# it: HOME, XDG_*, TMPDIR and MO2_VFS_STATE_DIR point inside it), it sets up from
# tests/fixtures/vfs-e2e:
# - a fake Skyrim Special Edition (SkyrimSE.exe, Data/Skyrim.esm, Data/original.txt,
#   Data/conflict.txt), accepted by the game_bethesda plugin,
# - a global instance (in $XDG_DATA_HOME/ModOrganizer/) with the mods ModA and ModB,
#   both enabled, ModB first in modlist.txt (highest priority), and probe.sh as the
#   configured executable "probe", with the data directory as argument,
# and runs `ModOrganizer -i vfs-e2e run -e probe` headless (offscreen Qt platform):
# the same ProcessRunner flow as the Run button (beforeRun mounts the file system,
# the program runs in a process group that mo2-vfs watches, afterRun waits for the
# unmount). It passes when:
# - the application exits with 0 within MO2_E2E_TIMEOUT seconds (120 by default),
# - every check of probe.sh (see there) passed, while the file system was mounted,
# - afterwards, overwrite/probe-created.txt has what the probe wrote, the real data
#   directory has exactly its original files with their original content, and
#   nothing is mounted under the temporary directory.
# Then the same run with MO2_VFS_BINARY pointing to a fake helper whose mount fails:
# beforeRun must return promptly (no dialog waits for an answer without a user
# interface): the run command logs "Failed to run" within MO2_E2E_FAIL_TIMEOUT seconds
# (30 by default), the reason of the failure is in the log, and the probe did not run.
# On failure it prints the probe results, the logs of mo2-vfs and of the application.
#
# Without /dev/fuse or fusermount3 the test is skipped with
# a message, unless MO2_REQUIRE_FUSE=1 (set in CI), where it fails.
set -euo pipefail

INSTALL="$(cd "${1:?usage: $0 <install prefix>}" && pwd)"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIXTURE="$HERE/../fixtures/vfs-e2e"
APP="$INSTALL/bin/ModOrganizer"
TIMEOUT="${MO2_E2E_TIMEOUT:-120}"

fail() { echo "vfs e2e test FAILED: $*" >&2; exit 1; }

[[ -x $APP ]] || fail "$APP not found"
[[ -x $INSTALL/bin/mo2-vfs ]] || fail "$INSTALL/bin/mo2-vfs not found"
compgen -G "$INSTALL/bin/plugins/libgame_skyrimse.so" >/dev/null ||
    fail "the Skyrim SE game plugin is not installed in $INSTALL/bin/plugins"

# like common::require_fuse() of the mo2-vfs tests: mo2-vfs needs both, even as root
missing=""
[[ -e /dev/fuse ]] || missing="/dev/fuse"
command -v fusermount3 >/dev/null || missing="${missing:+$missing and }fusermount3"
if [[ -n $missing ]]; then
    [[ ${MO2_REQUIRE_FUSE:-} == 1 ]] && fail "$missing is missing and MO2_REQUIRE_FUSE=1"
    echo "vfs e2e test SKIPPED: $missing is missing (set MO2_REQUIRE_FUSE=1 to fail instead)"
    exit 0
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/mo2-vfs-e2e.XXXXXX")"
HOMEDIR="$WORK/home"
INSTANCE="$HOMEDIR/.local/share/ModOrganizer/vfs-e2e"
GAME="$WORK/game"
PROBE_OUT="$WORK/probe.out"
APP_OUT="$WORK/app.out"

# mounts_under <dir>: the mo2-vfs mount points under <dir>, unprivileged (type
# fuse.mo2-vfs) or made by root (type fuse, source mo2-vfs)
mounts_under() {
    awk -v d="$1/" '
        { for (i = 7; $i != "-"; i++) ; type = $(i + 1); src = $(i + 2) }
        (type == "fuse.mo2-vfs" || (type == "fuse" && src == "mo2-vfs")) &&
            index($5 "/", d) == 1 { print $5 }' /proc/self/mountinfo
}

unmount() {
    if [[ $(id -u) == 0 ]]; then
        umount "$1" 2>/dev/null || umount -l "$1" 2>/dev/null || true
    else
        fusermount3 -u "$1" 2>/dev/null || fusermount3 -uz "$1" 2>/dev/null || true
    fi
}

# helpers: the pids of the mo2-vfs processes of this run (their command line has
# the log file, in the instance under $WORK)
helpers() {
    local d
    for d in /proc/[0-9]*; do
        [[ $(tr '\0' ' ' 2>/dev/null < "$d/cmdline") == "$INSTALL/bin/mo2-vfs "*"$WORK/"* ]] &&
            echo "${d#/proc/}"
    done
    return 0
}

# never removes the temporary directory through a live mount: a helper left behind
# (after a timeout) is stopped, whatever is still mounted under the directory is
# unmounted, and if that fails the directory is left
cleanup() {
    local rc=$? m left p
    for p in $(helpers); do
        echo "cleanup: stopping mo2-vfs ($p)" >&2
        kill -TERM "$p" 2>/dev/null || true
    done
    for _ in 1 2 3 4 5; do
        [[ -z $(helpers) ]] && break
        sleep 1
    done
    for p in $(helpers); do kill -KILL "$p" 2>/dev/null || true; done
    for _ in 1 2 3; do
        left="$(mounts_under "$WORK")"
        [[ -z $left ]] && break
        while read -r m; do
            echo "cleanup: unmounting $m" >&2
            unmount "$m"
        done <<< "$left"
        sleep 1
    done
    if [[ -n $(mounts_under "$WORK") ]]; then
        echo "cleanup: still mounted under $WORK, not removing it:" >&2
        mounts_under "$WORK" >&2
    else
        rm -rf "$WORK"
    fi
    exit "$rc"
}
trap cleanup EXIT

show_logs() {
    echo "--- probe results ($PROBE_OUT)"
    cat "$PROBE_OUT" 2>/dev/null || echo "(none)"
    local f
    # mo2-vfs.log (the helper) and the logs of the application
    for f in "$INSTANCE"/logs/*; do
        [[ -f $f ]] || continue
        echo "--- $f"
        cat "$f"
    done
    echo "--- application output"
    cat "$APP_OUT" 2>/dev/null || echo "(none)"
}

die() { show_logs >&2; fail "$*"; }

# --- set up ------------------------------------------------------------------------

mkdir -p "$HOMEDIR"/{.config,.cache,.local/share,.local/state} "$WORK/tmp" \
    "$WORK/state" "$WORK/bin"
mkdir -m 700 "$WORK/runtime"
install -m 755 "$HERE/probe.sh" "$WORK/bin/probe.sh"
# shellcheck source=../lib/instance.sh
source "$HERE/../lib/instance.sh"
mo2_make_instance "$FIXTURE" "$WORK" "$GAME" "$INSTANCE" "$WORK/bin/probe.sh"

# the real data directory, to compare with afterwards
original="$(cd "$GAME/Data" && LC_ALL=C find . -type f -exec md5sum {} + | sort)"

# --- run ---------------------------------------------------------------------------

echo "--- ModOrganizer -i vfs-e2e run -e probe (${TIMEOUT}s)"
rc=0
env HOME="$HOMEDIR" \
    XDG_CONFIG_HOME="$HOMEDIR/.config" XDG_DATA_HOME="$HOMEDIR/.local/share" \
    XDG_CACHE_HOME="$HOMEDIR/.cache" XDG_STATE_HOME="$HOMEDIR/.local/state" \
    XDG_RUNTIME_DIR="$WORK/runtime" TMPDIR="$WORK/tmp" \
    MO2_VFS_STATE_DIR="$WORK/state" MO2_VFS_BINARY="$INSTALL/bin/mo2-vfs" \
    PROBE_OUT="$PROBE_OUT" QT_QPA_PLATFORM=offscreen \
    timeout -k 10 "$TIMEOUT" "$APP" --logs -i vfs-e2e run -e probe \
    > "$APP_OUT" 2>&1 < /dev/null || rc=$?

((rc == 0)) || die "the application exited with $rc (124: timeout)"

# --- check -------------------------------------------------------------------------

[[ -s $PROBE_OUT ]] || die "the probe did not run (no $PROBE_OUT)"
cat "$PROBE_OUT"
grep -q '^done$' "$PROBE_OUT" || die "the probe did not finish"
! grep -q '^FAIL' "$PROBE_OUT" || die "a check of the probe failed"

created="$INSTANCE/overwrite/probe-created.txt"
[[ -f $created ]] || die "$created does not exist"
[[ $(cat "$created") == "created by the probe" ]] || die "$created has the wrong content"
echo "ok overwrite/probe-created.txt"

after="$(cd "$GAME/Data" && LC_ALL=C find . -type f -exec md5sum {} + | sort)"
[[ $after == "$original" ]] ||
    die "the real data directory changed:"$'\n'"before:"$'\n'"$original"$'\n'"after:"$'\n'"$after"
[[ -z $(cd "$GAME/Data" && find . -mindepth 1 -not -type f) ]] ||
    die "the real data directory has new entries"
echo "ok real data directory unchanged"

left="$(mounts_under "$WORK")"
[[ -z $left ]] || die "still mounted after the run: $left"
echo "ok nothing mounted"

# the helper ends with the application (bounded: 10 s)
for _ in $(seq 10); do
    [[ -z $(helpers) ]] && break
    sleep 1
done
[[ -z $(helpers) ]] || die "mo2-vfs still running after the application exited: $(helpers)"
echo "ok mo2-vfs ended"

# --- a helper whose mount fails --------------------------------------------------

fake="$WORK/bin/failing-mo2-vfs"
cat > "$fake" <<'FAKE'
#!/bin/sh
[ "$1" = "--cleanup" ] && exit 0
echo '{"event":"hello","version":"0"}'
read -r line
echo '{"event":"error","code":"io","message":"fake mount failure for the e2e test","path":null}'
read -r line
FAKE
chmod 755 "$fake"
rm -f "$PROBE_OUT"

FAIL_TIMEOUT="${MO2_E2E_FAIL_TIMEOUT:-30}"
echo "--- ModOrganizer -i vfs-e2e run -e probe, failing helper (${FAIL_TIMEOUT}s)"
start=$SECONDS
env HOME="$HOMEDIR" \
    XDG_CONFIG_HOME="$HOMEDIR/.config" XDG_DATA_HOME="$HOMEDIR/.local/share" \
    XDG_CACHE_HOME="$HOMEDIR/.cache" XDG_STATE_HOME="$HOMEDIR/.local/state" \
    XDG_RUNTIME_DIR="$WORK/runtime" TMPDIR="$WORK/tmp" \
    MO2_VFS_STATE_DIR="$WORK/state" MO2_VFS_BINARY="$fake" \
    PROBE_OUT="$PROBE_OUT" QT_QPA_PLATFORM=offscreen \
    timeout -k 5 "$((FAIL_TIMEOUT + 10))" "$APP" --logs -i vfs-e2e run -e probe \
    > "$APP_OUT" 2>&1 < /dev/null &
app=$!
# beforeRun returns at once (no dialog without a user interface): the run command then
# reports "Failed to run" (its own error dialog, upstream behaviour, is not waited for:
# the application is stopped once that line is logged)
log="$INSTANCE/logs/mo_interface.log"
returned=0
for ((i = 0; i < FAIL_TIMEOUT * 2; i++)); do
    grep -q "Failed to run 'probe'" "$log" 2>/dev/null && { returned=1; break; }
    kill -0 "$app" 2>/dev/null || { returned=1; break; }
    sleep 0.5
done
kill -TERM "$app" 2>/dev/null || true
wait "$app" 2>/dev/null || true
((returned)) ||
    die "the run did not return from the failed mount within ${FAIL_TIMEOUT}s"
grep -q "fake mount failure for the e2e test" "$log" ||
    die "the reason of the failed mount is not in the log"
[[ ! -e $PROBE_OUT ]] || die "the probe ran although the mount failed"
echo "ok failed mount: run returned in $((SECONDS - start))s, reason logged"

echo "vfs e2e test passed"
