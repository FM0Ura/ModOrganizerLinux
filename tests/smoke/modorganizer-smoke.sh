#!/usr/bin/env bash
# Smoke test of the installed application, headless (offscreen Qt platform):
#
#   tests/smoke/modorganizer-smoke.sh <install prefix>
#
# 1. `ModOrganizer --help` exits with 0 and prints the usage (it mentions --instance);
# 2. started with a fresh HOME and XDG directories (so there is no instance), the
#    application gets past the creation of its QApplication and reaches the instance
#    selection: it loads the plugins (log "looking for plugins in ..." and the timing of
#    PluginContainer::loadPlugins(), which selectInstance() does before showing the
#    create-instance dialog) and then waits in that dialog until the timeout
#    (MO2_SMOKE_TIMEOUT seconds, 20 by default) stops it with SIGTERM. A crash, an
#    abort or a missing Qt platform plugin fails the test; a clean exit is accepted.
#    This is done twice in a row, see the end of the script.
set -euo pipefail

INSTALL="${1:?usage: $0 <install prefix>}"
APP="$INSTALL/bin/ModOrganizer"
TIMEOUT="${MO2_SMOKE_TIMEOUT:-20}"

fail() { echo "smoke test FAILED: $*" >&2; exit 1; }

[[ -x $APP ]] || fail "$APP not found"

echo "--- ModOrganizer --help"
rc=0
out="$(QT_QPA_PLATFORM=offscreen "$APP" --help 2>&1)" || rc=$?
printf '%s\n' "$out"
((rc == 0)) || fail "--help exited with $rc"
[[ $out == *instance* ]] || fail "--help did not print the usage"

# start <n>: one headless start with a fresh HOME, stopped by the timeout
start() {
    local home rc=0 out
    echo "--- ModOrganizer, start $1 (fresh HOME, no instance, ${TIMEOUT}s)"
    home="$(mktemp -d)"
    out="$(env HOME="$home" \
        XDG_CONFIG_HOME="$home/.config" XDG_DATA_HOME="$home/.local/share" \
        XDG_CACHE_HOME="$home/.cache" XDG_STATE_HOME="$home/.local/state" \
        QT_QPA_PLATFORM=offscreen \
        timeout -k 10 "$TIMEOUT" "$APP" --logs 2>&1)" || rc=$?
    rm -rf "$home"
    printf '%s\n' "$out"

    [[ $out != *"Could not find the Qt platform plugin"* ]] || fail "no Qt platform plugin"
    [[ $out != *"has crashed"* && $out != *"terminate called"* ]] ||
        fail "the application crashed"
    # 124: stopped by the timeout (SIGTERM); 0: clean exit; anything else (134 abort,
    # 139 segmentation fault, 137 killed after not stopping on SIGTERM, ...) fails
    ((rc == 124 || rc == 0)) || fail "the application exited with $rc"
    [[ $out == *"looking for plugins in "* ]] || fail "the plugins were not loaded"
    [[ $out == *"timing: PluginContainer::loadPlugins()"* ]] ||
        fail "the instance selection was not reached"
    echo "start $1 passed (exit $rc)"
}

# twice: the second start must not be disturbed by what the first one, stopped by
# SIGTERM, left behind (single-process shared memory and local socket)
start 1
start 2

echo "smoke test passed"
