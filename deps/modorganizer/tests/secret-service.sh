#!/bin/sh
# Runs the Secret Service integration test of organizer_tests
# (SecretServiceIntegration.*, test_secretservice.cpp) against a gnome-keyring-daemon
# of its own, in a D-Bus session of its own, with a temporary HOME: the user's keyring
# is never touched. Exit 77 (ctest SKIP_RETURN_CODE) when the tools are missing.
#
#   tests/secret-service.sh <organizer_tests binary>
set -eu

TESTS="${1:?usage: $0 <organizer_tests binary>}"

if ! command -v dbus-run-session >/dev/null 2>&1 ||
    ! command -v gnome-keyring-daemon >/dev/null 2>&1; then
    echo "skipped: dbus-run-session or gnome-keyring-daemon not found"
    exit 77
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -m 700 "$work/runtime"

export HOME="$work" XDG_DATA_HOME="$work/data" XDG_CONFIG_HOME="$work/config" \
    XDG_RUNTIME_DIR="$work/runtime"
unset DISPLAY WAYLAND_DISPLAY

# --unlock reads the password of the new "login" keyring (the default collection)
# from stdin
out="$work/gtest.out"
rc=0
dbus-run-session -- sh -c '
    printf "mo2-tests" | gnome-keyring-daemon --unlock --components=secrets >/dev/null
    MO2_TESTS_SECRET_SERVICE=1 exec "$0" --gtest_filter="SecretServiceIntegration.*"
' "$TESTS" > "$out" 2>&1 || rc=$?
cat "$out"

# a filter that matches nothing (or a test that skips itself) also exits 0
if [ "$rc" -ne 0 ] || ! grep -q '^\[  PASSED  \] 1 test' "$out"; then
    echo "FAILED: the Secret Service integration test did not run and pass" >&2
    exit 1
fi
