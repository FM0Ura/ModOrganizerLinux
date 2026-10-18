#!/usr/bin/env bash
# The program that the end-to-end test (run.sh) makes Mod Organizer run, as a
# configured executable of the test instance, with the virtual file system:
#
#   probe.sh <data directory of the game>
#
# It writes one line per check to $PROBE_OUT ("ok <check>" or "FAIL <check>: ...")
# and "done" at the end; run.sh reads that file once the application has exited.
# What it checks, through the mo2-vfs view of the data directory:
# - the data directory is a mo2-vfs mount (/proc/self/mountinfo, both the
#   unprivileged form, type fuse.mo2-vfs, and the one made by root, type fuse with
#   source mo2-vfs),
# - it lists the files of the game (Skyrim.esm, original.txt) plus those of ModA
#   (a.txt) and ModB (b.txt), with conflict.txt, present in all three, once,
# - conflict.txt has the content of ModB (first in modlist.txt, so highest priority),
#   a.txt the content of ModA,
# - a file created in it (probe-created.txt) can be read back (it goes to overwrite/).
set -u

DATA="${1:?usage: $0 <data directory>}"
OUT="${PROBE_OUT:?PROBE_OUT is not set}"

: > "$OUT"
ok() { echo "ok $*" >> "$OUT"; }
bad() { echo "FAIL $*" >> "$OUT"; }

# mountinfo: field 5 is the mount point (space, tab, newline and backslash written as
# octal escapes: the game directory has spaces), after " - " come the type and the source
mnt="${DATA//\\/\\134}"
mnt="${mnt// /\\040}"
mnt="${mnt//$'\t'/\\011}"
mnt="${mnt//$'\n'/\\012}"
if MNT="$mnt" awk '
    { for (i = 7; $i != "-"; i++) ; type = $(i + 1); src = $(i + 2) }
    $5 == ENVIRON["MNT"] && (type == "fuse.mo2-vfs" || (type == "fuse" && src == "mo2-vfs")) { f = 1 }
    END { exit !f }' /proc/self/mountinfo; then
    ok "mounted"
else
    bad "mounted: $DATA is not a mo2-vfs mount"
fi

listing="$(cd "$DATA" && LC_ALL=C ls -A | tr '\n' ' ')"
expected="Skyrim.esm a.txt b.txt conflict.txt original.txt "
if [[ $listing == "$expected" ]]; then
    ok "listing"
else
    bad "listing: got '$listing', expected '$expected'"
fi

check_content() {
    local got
    got="$(cat "$DATA/$1" 2>&1)"
    if [[ $got == "$2" ]]; then
        ok "content $1"
    else
        bad "content $1: got '$got', expected '$2'"
    fi
}
check_content conflict.txt "from ModB"
check_content a.txt "from ModA"
check_content b.txt "from ModB"
check_content original.txt "original"

if echo "created by the probe" > "$DATA/probe-created.txt"; then
    check_content probe-created.txt "created by the probe"
else
    bad "create probe-created.txt"
fi

echo done >> "$OUT"
