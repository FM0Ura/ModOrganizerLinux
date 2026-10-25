#!/usr/bin/env bash
# Test of build/appimage/AppRun and of the QtWebEngineProcess wrapper (both source
# build/appimage/mo2-hostlibs.sh), without an AppImage: a fake AppDir whose
# usr/bin/ModOrganizer records its arguments and environment, a fake libstdc++.so.6 in
# its usr/lib and a fake host (an ldconfig found through PATH that lists fake host
# libraries). The libraries are text files with GLIBCXX_3.4.N version strings, which is
# all AppRun reads.
#
#   tests/appimage/test-apprun.sh
#
# Cases:
# - host libstdc++ newer: $XDG_RUNTIME_DIR/mo2-hostlibs-$UID has links to the host's
#   libstdc++.so.6 and libgcc_s.so.1 and is first in LD_LIBRARY_PATH, and
#   MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH has the previous value;
# - host libstdc++ equal or older, or not found: the environment is unchanged and
#   there is no marker;
# - host libstdc++ newer and LD_LIBRARY_PATH unset: the marker is MO2_APPIMAGE_UNSET;
# and in every case ModOrganizer gets the arguments unchanged and every other variable
# keeps its value. The wrapper of QtWebEngineProcess, started with the host's
# environment (what Mod Organizer restores), makes the same choice for the real
# QtWebEngineProcess: the same directory first in LD_LIBRARY_PATH when the host is newer,
# nothing changed otherwise, the arguments kept, no marker (argv[0] is kept too, with
# exec -a, which only an ELF shows: the fake one is a script).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APPRUN="$HERE/../../build/appimage/AppRun"
WRAPPER="$HERE/../../build/appimage/QtWebEngineProcess"
HOSTLIBS_SH="$HERE/../../build/appimage/mo2-hostlibs.sh"

fail() { echo "AppRun test FAILED: $*" >&2; exit 1; }

for f in "$APPRUN" "$WRAPPER" "$HOSTLIBS_SH"; do
    [[ -f $f ]] || fail "$f not found"
done

TMP="$(mktemp -d "${TMPDIR:-/tmp}/mo2-apprun-test.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# fake library: one GLIBCXX_3.4.N string per version up to the given one
fake_lib() {
    local file="$1" max="$2" n
    mkdir -p "$(dirname "$file")"
    : >"$file"
    for ((n = 0; n <= max; n++)); do
        printf 'GLIBCXX_3.4.%d\0' "$n" >>"$file"
    done
    printf 'CXXABI_1.3.15\0GLIBCXX_DEBUG_MESSAGE_LENGTH\0' >>"$file"
}

APPDIR="$TMP/AppDir"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/libexec"
cp "$APPRUN" "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"
cp "$HOSTLIBS_SH" "$APPDIR/usr/libexec/mo2-hostlibs.sh"
cp "$WRAPPER" "$APPDIR/usr/libexec/QtWebEngineProcess"
chmod +x "$APPDIR/usr/libexec/QtWebEngineProcess"
cat >"$APPDIR/usr/libexec/QtWebEngineProcess.real" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" >"$OUT/args"
env >"$OUT/env"
EOF
chmod +x "$APPDIR/usr/libexec/QtWebEngineProcess.real"
fake_lib "$APPDIR/usr/lib/libstdc++.so.6" 33
cat >"$APPDIR/usr/bin/ModOrganizer" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" >"$OUT/args"
env >"$OUT/env"
EOF
chmod +x "$APPDIR/usr/bin/ModOrganizer"

# run_apprun <case> <host GLIBCXX max, or none> [LD_LIBRARY_PATH]: runs AppRun with a
# clean environment and a fake ldconfig; leaves $TMP/<case>/{args,env,expected-env}.
# With PROGRAM set, runs it instead of AppRun (without APPDIR, as Mod Organizer does)
run_apprun() {
    local name="$1" hostmax="$2"
    local dir="$TMP/$name"
    mkdir -p "$dir/bin" "$dir/host/lib64" "$dir/host/lib" "$dir/run"

    if [[ $hostmax == none ]]; then
        printf '#!/bin/sh\necho "0 libs found in cache"\n' >"$dir/bin/ldconfig"
    else
        fake_lib "$dir/host/lib64/libstdc++.so.6" "$hostmax"
        echo "host libgcc_s" >"$dir/host/lib64/libgcc_s.so.1"
        # a 32-bit libstdc++ first, newer still: AppRun must take the x86-64 one
        fake_lib "$dir/host/lib/libstdc++.so.6" 99
        cat >"$dir/bin/ldconfig" <<EOF
#!/bin/sh
[ "\$1" = -p ] || exit 1
echo "1234 libs found in cache \\\`/etc/ld.so.cache'"
printf '\tlibstdc++.so.6 (libc6) => %s\n' "$dir/host/lib/libstdc++.so.6"
printf '\tlibstdc++.so.6 (libc6,x86-64) => %s\n' "$dir/host/lib64/libstdc++.so.6"
printf '\tlibgcc_s.so.1 (libc6,x86-64) => %s\n' "$dir/host/lib64/libgcc_s.so.1"
EOF
    fi
    chmod +x "$dir/bin/ldconfig"

    local -a vars=(
        "PATH=$dir/bin:/usr/bin:/bin"
        "HOME=$dir"
        "XDG_RUNTIME_DIR=$dir/run"
        "OUT=$dir"
        "MO2_TEST_VAR=kept value"
    )
    [[ -n ${PROGRAM:-} ]] || vars+=("APPDIR=$APPDIR")
    if (($# > 2)); then
        vars+=("LD_LIBRARY_PATH=$3")
    fi

    local program="${PROGRAM:-$APPDIR/AppRun}"
    env -i "${vars[@]}" "$program" first "second arg" ||
        fail "$name: $program exited with $?"
    [[ -f $dir/env ]] || fail "$name: the program behind $program did not run"
    [[ $(cat "$dir/args") == $'first\nsecond arg' ]] ||
        fail "$name: arguments changed: $(cat "$dir/args")"

    # what ModOrganizer would see without AppRun (and the variables bash itself sets)
    env -i "${vars[@]}" bash -c 'env' >"$dir/expected-env"
}

# value <case> <variable>: the value in the environment of ModOrganizer, or <unset>
value() {
    local line
    line="$(grep -E "^$2=" "$TMP/$1/env" || true)"
    if [[ -z $line ]]; then
        echo "<unset>"
    else
        echo "${line#*=}"
    fi
}

# others_unchanged <case>: every variable but LD_LIBRARY_PATH and the marker is as
# without AppRun (bash's own SHLVL, _ and PWD are ignored)
others_unchanged() {
    local filter='^(LD_LIBRARY_PATH|MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH|SHLVL|_|PWD|OLDPWD)='
    diff <(grep -Ev "$filter" "$TMP/$1/expected-env" | sort) \
        <(grep -Ev "$filter" "$TMP/$1/env" | sort) >&2 ||
        fail "$1: other variables changed (above)"
}

HOSTLIBS_NAME="mo2-hostlibs-$(id -u)"

# --- host newer -------------------------------------------------------------
run_apprun newer 34 "/old/path:/other"
hostlibs="$TMP/newer/run/$HOSTLIBS_NAME"
[[ $(value newer LD_LIBRARY_PATH) == "$hostlibs:/old/path:/other" ]] ||
    fail "newer: LD_LIBRARY_PATH is '$(value newer LD_LIBRARY_PATH)'"
[[ $(value newer MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH) == "/old/path:/other" ]] ||
    fail "newer: marker is '$(value newer MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH)'"
[[ $(readlink "$hostlibs/libstdc++.so.6") == "$TMP/newer/host/lib64/libstdc++.so.6" ]] ||
    fail "newer: libstdc++.so.6 link is '$(readlink "$hostlibs/libstdc++.so.6" || true)'"
[[ $(readlink "$hostlibs/libgcc_s.so.1") == "$TMP/newer/host/lib64/libgcc_s.so.1" ]] ||
    fail "newer: libgcc_s.so.1 link is '$(readlink "$hostlibs/libgcc_s.so.1" || true)'"
others_unchanged newer
echo "ok - newer host libstdc++ goes first in LD_LIBRARY_PATH"

# --- host equal -------------------------------------------------------------
run_apprun equal 33 "/old/path"
[[ $(value equal LD_LIBRARY_PATH) == "/old/path" ]] ||
    fail "equal: LD_LIBRARY_PATH is '$(value equal LD_LIBRARY_PATH)'"
[[ $(value equal MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH) == "<unset>" ]] ||
    fail "equal: marker is '$(value equal MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH)'"
[[ ! -e $TMP/equal/run/$HOSTLIBS_NAME ]] || fail "equal: $HOSTLIBS_NAME was created"
others_unchanged equal
echo "ok - equal host libstdc++ leaves the environment unchanged"

# --- host older -------------------------------------------------------------
run_apprun older 29
[[ $(value older LD_LIBRARY_PATH) == "<unset>" ]] ||
    fail "older: LD_LIBRARY_PATH is '$(value older LD_LIBRARY_PATH)'"
[[ $(value older MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH) == "<unset>" ]] ||
    fail "older: marker is '$(value older MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH)'"
others_unchanged older
echo "ok - older host libstdc++ leaves the environment unchanged"

# --- host libstdc++ not found -----------------------------------------------
run_apprun missing none "/old/path"
[[ $(value missing LD_LIBRARY_PATH) == "/old/path" ]] ||
    fail "missing: LD_LIBRARY_PATH is '$(value missing LD_LIBRARY_PATH)'"
[[ $(value missing MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH) == "<unset>" ]] ||
    fail "missing: marker is '$(value missing MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH)'"
others_unchanged missing
echo "ok - no host libstdc++ leaves the environment unchanged"

# --- host newer, LD_LIBRARY_PATH unset --------------------------------------
run_apprun unset 34
[[ $(value unset LD_LIBRARY_PATH) == "$TMP/unset/run/$HOSTLIBS_NAME" ]] ||
    fail "unset: LD_LIBRARY_PATH is '$(value unset LD_LIBRARY_PATH)'"
[[ $(value unset MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH) == "MO2_APPIMAGE_UNSET" ]] ||
    fail "unset: marker is '$(value unset MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH)'"
others_unchanged unset
echo "ok - unset LD_LIBRARY_PATH is marked MO2_APPIMAGE_UNSET"

# --- the wrapper of QtWebEngineProcess ----------------------------------------
PROGRAM="$APPDIR/usr/libexec/QtWebEngineProcess"

run_apprun webengine-newer 34 "/host/path"
hostlibs="$TMP/webengine-newer/run/$HOSTLIBS_NAME"
[[ $(value webengine-newer LD_LIBRARY_PATH) == "$hostlibs:/host/path" ]] ||
    fail "webengine-newer: LD_LIBRARY_PATH is '$(value webengine-newer LD_LIBRARY_PATH)'"
[[ $(readlink "$hostlibs/libstdc++.so.6") == "$TMP/webengine-newer/host/lib64/libstdc++.so.6" ]] ||
    fail "webengine-newer: libstdc++.so.6 link is '$(readlink "$hostlibs/libstdc++.so.6" || true)'"
[[ $(value webengine-newer MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH) == "<unset>" ]] ||
    fail "webengine-newer: the wrapper set a marker"
others_unchanged webengine-newer
echo "ok - the QtWebEngineProcess wrapper puts the newer host libstdc++ first"

run_apprun webengine-unset 34
[[ $(value webengine-unset LD_LIBRARY_PATH) == "$TMP/webengine-unset/run/$HOSTLIBS_NAME" ]] ||
    fail "webengine-unset: LD_LIBRARY_PATH is '$(value webengine-unset LD_LIBRARY_PATH)'"
others_unchanged webengine-unset
echo "ok - the QtWebEngineProcess wrapper with LD_LIBRARY_PATH unset"

run_apprun webengine-equal 33 "/host/path"
[[ $(value webengine-equal LD_LIBRARY_PATH) == "/host/path" ]] ||
    fail "webengine-equal: LD_LIBRARY_PATH is '$(value webengine-equal LD_LIBRARY_PATH)'"
[[ ! -e $TMP/webengine-equal/run/$HOSTLIBS_NAME ]] || fail "webengine-equal: $HOSTLIBS_NAME was created"
others_unchanged webengine-equal
echo "ok - the QtWebEngineProcess wrapper leaves the environment unchanged otherwise"

run_apprun webengine-missing none
[[ $(value webengine-missing LD_LIBRARY_PATH) == "<unset>" ]] ||
    fail "webengine-missing: LD_LIBRARY_PATH is '$(value webengine-missing LD_LIBRARY_PATH)'"
others_unchanged webengine-missing
echo "ok - the QtWebEngineProcess wrapper without a host libstdc++"
unset PROGRAM

echo "AppRun test passed"
