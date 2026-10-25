#!/usr/bin/env bash
# Smoke test of the installed application, headless (offscreen Qt platform):
#
#   tests/smoke/modorganizer-smoke.sh <install prefix>
#
# 1. `ModOrganizer --help` exits with 0 and prints the usage (it mentions --instance);
# 1b. without a display (WAYLAND_DISPLAY, DISPLAY and QT_QPA_PLATFORM unset), it exits
#    with 1 and says it needs a graphical session, without crashing (Phase 4c.2);
# 2. started with a fresh HOME and XDG directories (also XDG_RUNTIME_DIR and
#    MO2_VFS_STATE_DIR; so there is no instance), the
#    application gets past the creation of its QApplication and reaches the instance
#    selection: it loads the plugins (log "looking for plugins in ..." and the timing of
#    PluginContainer::loadPlugins(), which selectInstance() does before showing the
#    create-instance dialog) and then waits in that dialog until the timeout
#    (MO2_SMOKE_TIMEOUT seconds, 20 by default) stops it with SIGTERM. A crash, an
#    abort or a missing Qt platform plugin fails the test; a clean exit is accepted.
#    This is done twice in a row, see the end of the script.
# 3. with a ready instance (the fake Skyrim SE and the instance of
#    tests/fixtures/vfs-e2e, set up in a temporary directory that also holds HOME,
#    XDG_* and TMPDIR), `ModOrganizer -i smoke` gets up to the main window: the
#    application logs "displaying main window" right before MainWindow::show()
#    (deps/modorganizer/src/moapplication.cpp, MOApplication::doOneRun()). The test then
#    sends SIGTERM (30 s at most to reach the window, 10 s to exit) and checks that the
#    application did not crash (no crash dump, exit by TERM or 0). Nothing may be
#    written in the real HOME (checked with a marker file). It needs the installed
#    Skyrim SE plugin; MO2_SMOKE_MAIN_WINDOW=0 skips it (build.sh does that when the
#    plugin is not installed). While it runs, a second `ModOrganizer nxm://...` must exit
#    with 0 and the running one must log `download requested: nxm://...` (the nxm://
#    handler, spec of Phase 4a).
#    Every plugin of bin/plugins/*.so must be logged as loaded by the application
#    ("loaded plugin '...' from '<file>.so'") and none may log a load/initialisation failure.
#    With the Python proxy installed (Phase 4d.1), Python must have started (no "failed to
#    init python") and every Python plugin (bin/plugins/*.py, bin/plugins/*/__init__.py,
#    but not plugin_python/ nor data/) must be logged as loaded from its file or folder.
# 4. restart in the same process (Phase 5, spec section 3.8): with a ready instance and
#    MO2_SMOKE_RESTART_ONCE=1, MOApplication::doOneRun() makes the application restart once
#    right after showing the main window (what Change Game does). The test waits for two
#    "displaying main window" lines and then sends SIGTERM; a crash (the second
#    aboutToQuit used to segfault in OzoneUtilQt::getQOpenGLContext), a crash dump or an
#    exit that is not by TERM or 0 fails it.
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

echo "--- ModOrganizer without a display"
rc=0
nd_home="$(mktemp -d)"
mkdir -m 700 "$nd_home/runtime"
out="$(env -u WAYLAND_DISPLAY -u DISPLAY -u QT_QPA_PLATFORM HOME="$nd_home" \
    XDG_CONFIG_HOME="$nd_home/.config" XDG_DATA_HOME="$nd_home/.local/share" \
    XDG_CACHE_HOME="$nd_home/.cache" XDG_STATE_HOME="$nd_home/.local/state" \
    XDG_RUNTIME_DIR="$nd_home/runtime" MO2_VFS_STATE_DIR="$nd_home/runtime/mo2-vfs" \
    DBUS_SESSION_BUS_ADDRESS="unix:path=$nd_home/no-bus" \
    timeout -k 5 20 "$APP" 2>&1)" || rc=$?
rm -rf "$nd_home"
printf '%s\n' "$out"
((rc == 1)) || fail "without a display, it exited with $rc instead of 1"
[[ $out == *"needs a graphical session"* ]] || fail "without a display, no message"
[[ $out != *"has crashed"* && $out != *"terminate called"* ]] ||
    fail "without a display, the application crashed"

# start <n>: one headless start with a fresh HOME, stopped by the timeout
start() {
    local home rc=0 out
    echo "--- ModOrganizer, start $1 (fresh HOME, no instance, ${TIMEOUT}s)"
    home="$(mktemp -d)"
    # a private runtime directory too: neither it nor the state directory of mo2-vfs
    # (read by its --cleanup at startup) may be the user's
    mkdir -m 700 "$home/runtime"
    # logins are in the Secret Service: a bus that does not exist keeps the test away
    # from the keyring of the user (the box reaches the session bus of the host)
    out="$(env HOME="$home" \
        XDG_CONFIG_HOME="$home/.config" XDG_DATA_HOME="$home/.local/share" \
        XDG_CACHE_HOME="$home/.cache" XDG_STATE_HOME="$home/.local/state" \
        XDG_RUNTIME_DIR="$home/runtime" MO2_VFS_STATE_DIR="$home/runtime/mo2-vfs" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=$home/no-bus" \
        MO2_VFS_BRIDGE=direct QT_QPA_PLATFORM=offscreen \
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

# main_window: see 3. above
main_window() {
    # work and pid are global: the EXIT trap uses them after fail() left the function
    local here home instance game marker out rc=0 i found=0 f app_env link second rc2 got so
    local -a missing=()
    here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    echo "--- ModOrganizer -i smoke (ready instance, up to the main window)"
    pid=""
    work="$(mktemp -d "${TMPDIR:-/tmp}/mo2-smoke.XXXXXX")"
    # cannot abort: no failing command (or unset variable) may hide the test result
    trap '[[ -z ${pid:-} ]] || kill -KILL "$pid" 2>/dev/null; rm -rf "${work:-/nonexistent}" || true' EXIT
    home="$work/home"
    instance="$home/.local/share/ModOrganizer/smoke"
    game="$work/game"
    out="$work/app.out"
    mkdir -p "$home"/{.config,.cache,.local/share,.local/state} "$work/tmp"
    mkdir -m 700 "$work/runtime"
    # shellcheck source=../lib/instance.sh
    source "$here/../lib/instance.sh"
    mo2_make_instance "$here/../fixtures/vfs-e2e" "$work" "$game" "$instance" /bin/true

    # neither the first start nor an instance of a version before 2.5: those show modal
    # dialogs (tutorial, category setup/migration) that wait for an answer in
    # MainWindow::showEvent() (deps/modorganizer/src/mainwindow.cpp)
    # and the debug log level (log_level=0), "displaying main window" being a debug message
    sed -i -e 's|^\[General\]$|&\nfirst_start=false\nversion=2.5.2|' \
        -e 's|^\[Settings\]$|&\nlog_level=0|' "$instance/ModOrganizer.ini"

    marker="$work/marker"
    touch "$marker"
    sleep 1

    # QtWebEngine (the main window's Nexus/browser parts) refuses to run as root (CI
    # container) without --no-sandbox; the application has no --disable-gpu option, the
    # Chromium flag is the equivalent
    # no session bus: logins are in the Secret Service, the test must not touch the
    # keyring of the user
    app_env=(HOME="$home"
        XDG_CONFIG_HOME="$home/.config" XDG_DATA_HOME="$home/.local/share"
        XDG_CACHE_HOME="$home/.cache" XDG_STATE_HOME="$home/.local/state"
        XDG_RUNTIME_DIR="$work/runtime" TMPDIR="$work/tmp"
        DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-bus"
        MO2_VFS_BRIDGE=direct QT_QPA_PLATFORM=offscreen
        QTWEBENGINE_CHROMIUM_FLAGS="--no-sandbox --disable-gpu")
    env "${app_env[@]}" "$APP" --logs -i smoke > "$out" 2>&1 < /dev/null &
    pid=$!

    for ((i = 0; i < 60; i++)); do
        # the stdout copy of the log is block buffered into a file, the log file is not
        grep -q "displaying main window" "$instance"/logs/mo_interface.log "$out" 2>/dev/null &&
            { found=1; break; }
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.5
    done
    if ((found)); then
        # let the event loop start before stopping it
        sleep 2
        # spec of Phase 4a, section 4.3: a second invocation with an nxm:// link (what
        # the desktop file of the handler runs) is forwarded to the running application
        # (MOMultiProcess, same TMPDIR) and exits; the running one logs the download
        echo "--- ModOrganizer nxm://... (forwarded to the running application)"
        link='nxm://skyrimspecialedition/mods/1/files/2?key=k&expires=1&user_id=1'
        second="$work/second.out"
        rc2=0
        env "${app_env[@]}" timeout -k 5 20 "$APP" "$link" > "$second" 2>&1 < /dev/null ||
            rc2=$?
        cat "$second"
        ((rc2 == 0)) || fail "the nxm:// invocation exited with $rc2"
        got=0
        for ((i = 0; i < 20; i++)); do
            grep -qF "download requested: $link" "$instance"/logs/mo_interface.log "$out" \
                2>/dev/null && { got=1; break; }
            sleep 0.5
        done
        ((got)) || fail "the running application did not receive the nxm:// link"
        echo "nxm:// link forwarded"
        kill -TERM "$pid" 2>/dev/null || true
        for ((i = 0; i < 20; i++)); do
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.5
        done
    fi
    if kill -0 "$pid" 2>/dev/null; then
        kill -KILL "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        pid=""
        cat "$out"
        ((found)) || fail "the main window was not reached within 30 s"
        fail "the application did not exit within 10 s of SIGTERM"
    fi
    wait "$pid" || rc=$?
    pid=""
    cat "$out"

    ((found)) || fail "the main window was not reached (no \"displaying main window\" log)"
    # 143: SIGTERM (default action or Qt re-raising it); 0: clean exit
    ((rc == 143 || rc == 0)) || fail "the application exited with $rc after SIGTERM"
    [[ $(<"$out") != *"has crashed"* && $(<"$out") != *"terminate called"* ]] ||
        fail "the application crashed"
    f="$(find "$work" -type f \( -name '*.dmp' -o -name 'core' -o -name 'core.*' \) | head -n 5)"
    [[ -z $f ]] || fail "crash dump(s) written: $f"

    # every plugin of bin/plugins was loaded by the application (PluginContainer logs
    # "loaded plugin '<name>@<version>' from '<file>' - [...]"), and none failed: a
    # plugin can load in QPluginLoader (tests/plugin-load) and still fail its init()
    for so in "$INSTALL"/bin/plugins/*.so; do
        grep -qF "from '$(basename "$so")'" "$instance"/logs/mo_interface.log ||
            missing+=("$(basename "$so")")
    done
    ((${#missing[@]} == 0)) || fail "plugins not loaded by the application: ${missing[*]}"
    if grep -E "failed to load plugin|failed to initialize|failed to load \(may be outdated\)" \
        "$instance"/logs/mo_interface.log; then
        fail "a plugin failed to load (see above)"
    fi
    echo "all $(ls "$INSTALL"/bin/plugins/*.so | wc -l) plugins loaded"

    # the Python plugins (Phase 4d.1): Python started, and every .py plugin of
    # bin/plugins (and every plugin package, a folder with an __init__.py) was logged as
    # loaded through the Python proxy ("loaded plugin '<name>@<version>' from '<file>'",
    # with the file name of a simple plugin or the folder name of a package)
    if [[ -e "$INSTALL/bin/plugins/plugin_python.so" ]]; then
        ! grep -qF "failed to init python" "$instance"/logs/mo_interface.log "$out" ||
            fail "Python did not start"
        local py pyname missing_py=()
        for py in "$INSTALL"/bin/plugins/*.py "$INSTALL"/bin/plugins/*/__init__.py; do
            [[ -e $py ]] || continue
            # not plugins: the proxy's own files and the data folders of the simple plugins
            pyname="${py#"$INSTALL"/bin/plugins/}"
            [[ $pyname == plugin_python/* || $pyname == data/* ]] && continue
            pyname="${pyname%/__init__.py}"
            grep -qF "from '$pyname'" "$instance"/logs/mo_interface.log ||
                missing_py+=("$pyname")
        done
        ((${#missing_py[@]} == 0)) ||
            fail "Python plugins not loaded by the application: ${missing_py[*]}"
        echo "all Python plugins loaded"
    fi

    # hermetic: nothing written in the real HOME. In CI (container HOME) the whole of
    # .config, .local and .cache is checked; elsewhere only the paths of the application
    local -a dirs=()
    local d
    for d in .config .local .cache; do
        [[ -d $HOME/$d ]] || continue
        if [[ -n ${CI:-} || ${MO2_SMOKE_STRICT_HOME:-} == 1 ]]; then
            dirs+=("$HOME/$d")
        else
            while IFS= read -r f; do dirs+=("$f"); done < <(
                find "$HOME/$d" -maxdepth 3 -iname 'ModOrganizer*' 2>/dev/null)
        fi
    done
    if ((${#dirs[@]})); then
        f="$(find "${dirs[@]}" -newer "$marker" 2>/dev/null | head -n 20 || true)"
        [[ -z $f ]] || fail "the real HOME was written to: $f"
    fi
    echo "main window test passed (exit $rc, real HOME untouched)"
    trap - EXIT
    rm -rf "$work" || true
}

# restart_once: see 4. above
restart_once() {
    local here home instance game out rc=0 i n=0 f app_env
    here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    echo "--- ModOrganizer -i smoke, restarted once in the same process"
    pid=""
    work="$(mktemp -d "${TMPDIR:-/tmp}/mo2-smoke.XXXXXX")"
    trap '[[ -z ${pid:-} ]] || kill -KILL "$pid" 2>/dev/null; rm -rf "${work:-/nonexistent}" || true' EXIT
    home="$work/home"
    instance="$home/.local/share/ModOrganizer/smoke"
    game="$work/game"
    out="$work/app.out"
    mkdir -p "$home"/{.config,.cache,.local/share,.local/state} "$work/tmp"
    mkdir -m 700 "$work/runtime"
    # shellcheck source=../lib/instance.sh
    source "$here/../lib/instance.sh"
    mo2_make_instance "$here/../fixtures/vfs-e2e" "$work" "$game" "$instance" /bin/true
    sed -i -e 's|^\[General\]$|&\nfirst_start=false\nversion=2.5.2|' \
        -e 's|^\[Settings\]$|&\nlog_level=0|' "$instance/ModOrganizer.ini"
    app_env=(HOME="$home"
        XDG_CONFIG_HOME="$home/.config" XDG_DATA_HOME="$home/.local/share"
        XDG_CACHE_HOME="$home/.cache" XDG_STATE_HOME="$home/.local/state"
        XDG_RUNTIME_DIR="$work/runtime" TMPDIR="$work/tmp"
        DBUS_SESSION_BUS_ADDRESS="unix:path=$work/no-bus"
        MO2_VFS_BRIDGE=direct QT_QPA_PLATFORM=offscreen MO2_SMOKE_RESTART_ONCE=1
        QTWEBENGINE_CHROMIUM_FLAGS="--no-sandbox --disable-gpu")
    env "${app_env[@]}" "$APP" --logs -i smoke > "$out" 2>&1 < /dev/null &
    pid=$!

    # each restart starts a new log file, so the lines of the first run can be gone from
    # it: count the increments seen in the log file (c, base) and, as a lower bound, the
    # lines of the stdout copy (block buffered)
    local c=0 prev=0 base=0 o=0
    for ((i = 0; i < 120; i++)); do
        c="$(grep -c "displaying main window" "$instance"/logs/mo_interface.log 2>/dev/null ||
            true)"
        c="${c:-0}"
        ((c < prev)) && base=$((base + prev))
        prev=$c
        o="$(grep -c "displaying main window" "$out" 2>/dev/null || true)"
        n=$((base + c))
        ((o > n)) && n=$o
        ((n >= 2)) && break
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.5
    done
    if ((n >= 2)) && kill -0 "$pid" 2>/dev/null; then
        # let the second run settle before stopping it
        sleep 2
        kill -TERM "$pid" 2>/dev/null || true
        for ((i = 0; i < 20; i++)); do
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.5
        done
    fi
    if kill -0 "$pid" 2>/dev/null; then
        kill -KILL "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        pid=""
        cat "$out"
        fail "the application did not exit within 10 s of SIGTERM (or never restarted)"
    fi
    wait "$pid" || rc=$?
    pid=""
    cat "$out"
    n="$(grep -c "displaying main window" "$out" || true)"

    ((n >= 2)) || fail "the application did not show the main window twice (saw $n; exit $rc)"
    ((rc == 143 || rc == 0)) || fail "the application exited with $rc after the restart"
    [[ $(<"$out") != *"has crashed"* && $(<"$out") != *"terminate called"* ]] ||
        fail "the application crashed"
    f="$(find "$work" -type f \( -name '*.dmp' -o -name 'core' -o -name 'core.*' \) | head -n 5)"
    [[ -z $f ]] || fail "crash dump(s) written: $f"
    echo "restart test passed (exit $rc)"
    trap - EXIT
    rm -rf "$work" || true
}

# twice: the second start must not be disturbed by what the first one, stopped by
# SIGTERM, left behind (single-process shared memory and local socket)
start 1
start 2
if [[ ${MO2_SMOKE_MAIN_WINDOW:-1} == 0 ]]; then
    echo "--- main window test skipped (MO2_SMOKE_MAIN_WINDOW=0: no game plugin installed)"
else
    main_window
    restart_once
fi

echo "smoke test passed"
