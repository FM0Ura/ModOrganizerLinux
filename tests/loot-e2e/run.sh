#!/usr/bin/env bash
# End-to-end test of LOOT on Linux: lootcli, started by mo2-vfs with the virtual file
# system mounted, sorts the plugins of the mods of a fake Skyrim SE (see drive.py).
#
#   tests/loot-e2e/run.sh <install prefix> <cache directory>
#
# The plugins come from testing-plugins 1.6.2 (the plugins of libloot's tests),
# downloaded once into the cache directory and checked by SHA-256. Everything the
# test writes stays in a temporary directory, removed at the end unless something is
# still mounted there. Without /dev/fuse or fusermount3 the test is skipped with a
# message, unless MO2_REQUIRE_FUSE=1 (set in CI), where it fails.
set -euo pipefail

INSTALL="$(cd "${1:?usage: $0 <install prefix> <cache directory>}" && pwd -P)"
CACHE="${2:?usage: $0 <install prefix> <cache directory>}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TP_VERSION=1.6.2
TP_SHA256=f6e5b55e2669993ab650ba470424b725d1fab71ace979134a77de3373bd55620

fail() { echo "loot e2e test FAILED: $*" >&2; exit 1; }

[[ -x $INSTALL/bin/mo2-vfs ]] || fail "$INSTALL/bin/mo2-vfs not found"
[[ -x $INSTALL/bin/loot/lootcli ]] || fail "$INSTALL/bin/loot/lootcli not found"

missing=""
[[ -e /dev/fuse ]] || missing="/dev/fuse"
command -v fusermount3 >/dev/null || missing="${missing:+$missing and }fusermount3"
if [[ -n $missing ]]; then
    [[ ${MO2_REQUIRE_FUSE:-} == 1 ]] && fail "$missing is missing and MO2_REQUIRE_FUSE=1"
    echo "loot e2e test SKIPPED: $missing is missing (set MO2_REQUIRE_FUSE=1 to fail instead)"
    exit 0
fi

# --- testing-plugins ---------------------------------------------------------------
mkdir -p "$CACHE"
plugins="$CACHE/testing-plugins-$TP_VERSION/SkyrimSE/Data"
if [[ ! -f $plugins/Blank.esp ]]; then
    archive="$CACHE/testing-plugins-$TP_VERSION.tar.gz"
    curl -sSfL --retry 3 --max-time 120 -o "$archive" \
        "https://github.com/Ortham/testing-plugins/archive/refs/tags/$TP_VERSION.tar.gz"
    echo "$TP_SHA256  $archive" | sha256sum -c --quiet - ||
        fail "testing-plugins $TP_VERSION does not match its SHA-256"
    tar -C "$CACHE" -xzf "$archive"
fi

# --- run ---------------------------------------------------------------------------
WORK="$(mktemp -d "${TMPDIR:-/tmp}/mo2-loot-e2e.XXXXXX")"
# physical path: drive.py resolves it and mountinfo lists resolved paths, so under a
# symlinked TMPDIR the mount and process lookups below would otherwise miss
WORK="$(cd "$WORK" && pwd -P)"

# mounts_under <dir>: the mo2-vfs mount points under <dir>, unprivileged (type
# fuse.mo2-vfs) or made by root (type fuse, source mo2-vfs), with the octal escapes of
# mountinfo (the test's directories have spaces) decoded
mounts_under() {
    awk -v d="$1/" '
        { for (i = 7; $i != "-"; i++) ; type = $(i + 1); src = $(i + 2) }
        (type == "fuse.mo2-vfs" || (type == "fuse" && src == "mo2-vfs")) &&
            index($5 "/", d) == 1 {
            p = $5
            gsub(/\\040/, " ", p); gsub(/\\011/, "\t", p); gsub(/\\012/, "\n", p)
            gsub(/\\134/, "\\", p)
            print p }' /proc/self/mountinfo
}

unmount() {
    if [[ $(id -u) == 0 ]]; then
        umount "$1" 2>/dev/null || umount -l "$1" 2>/dev/null || true
    else
        fusermount3 -u "$1" 2>/dev/null || fusermount3 -uz "$1" 2>/dev/null || true
    fi
}

# helpers: the pids of the processes of this run: mo2-vfs (its command line has the
# log file in $WORK) and the lootcli it started (its command line has paths in $WORK)
helpers() {
    local d cmd
    for d in /proc/[0-9]*; do
        cmd="$(tr '\0' ' ' 2>/dev/null < "$d/cmdline")"
        [[ $cmd == "$INSTALL/bin/mo2-vfs "*"$WORK/"* ||
            $cmd == "$INSTALL/bin/loot/lootcli "*"$WORK/"* ]] && echo "${d#/proc/}"
    done
    return 0
}

# never removes the temporary directory through a live mount: a process left behind
# (after a failure or a timeout) is stopped, whatever is still mounted under the
# directory is unmounted, and if that fails the directory is left
cleanup() {
    local rc=$? m left p
    for p in $(helpers); do
        echo "cleanup: stopping $p" >&2
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

if ! python3 -I "$HERE/drive.py" "$INSTALL" "$plugins" "$WORK"; then
    echo "--- mo2-vfs.log" >&2
    cat "$WORK/mo2-vfs.log" >&2 2>/dev/null || true
    exit 1
fi
[[ -z $(mounts_under "$WORK") ]] || fail "still mounted under $WORK after the test"
