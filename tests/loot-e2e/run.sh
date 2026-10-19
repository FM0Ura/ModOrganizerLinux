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

INSTALL="$(cd "${1:?usage: $0 <install prefix> <cache directory>}" && pwd)"
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
    curl -sSfL -o "$archive" \
        "https://github.com/Ortham/testing-plugins/archive/refs/tags/$TP_VERSION.tar.gz"
    echo "$TP_SHA256  $archive" | sha256sum -c --quiet - ||
        fail "testing-plugins $TP_VERSION does not match its SHA-256"
    tar -C "$CACHE" -xzf "$archive"
fi

# --- run ---------------------------------------------------------------------------
WORK="$(mktemp -d "${TMPDIR:-/tmp}/mo2-loot-e2e.XXXXXX")"

mounts_under() {
    awk -v d="$1/" '{ if (index($5 "/", d) == 1) print $5 }' /proc/self/mountinfo
}

cleanup() {
    local rc=$? m
    while read -r m; do
        [[ -n $m ]] || continue
        echo "cleanup: unmounting $m" >&2
        if [[ $(id -u) == 0 ]]; then umount -l "$m" || true; else fusermount3 -uz "$m" || true; fi
    done <<< "$(mounts_under "$WORK")"
    if [[ -n $(mounts_under "$WORK") ]]; then
        echo "cleanup: still mounted under $WORK, not removing it" >&2
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
