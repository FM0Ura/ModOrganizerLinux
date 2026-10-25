#!/usr/bin/env bash
# Checks the AppDir of the Mod Organizer AppImage (spec of Phase 5, sections 2.3, 3.1
# and 3.2); build/appimage/build-appimage.sh runs it before making the AppImage.
#
#   scripts/check-appdir.sh <AppDir> <max-glibc>
#
# <max-glibc> is the glibc of the build (Fedora 44: `ldd --version`, e.g. 2.43). Fails,
# listing every problem, when:
# (a) an ELF needs a GLIBC_ symbol version above <max-glibc> (objdump -T);
# (b) with no environment (env -i, only PATH), an ELF does not find a library, or one of
#     the libraries it needs comes from outside the AppDir without being one the host
#     must provide: glibc, the graphics stack (libGL*, libEGL*, libOpenGL*, libvulkan*,
#     libgbm*, libdrm*), the display (libX11*, libxcb*, libwayland-*, libxkbcommon*),
#     libfontconfig, libfreetype and libdbus-1; libz and everything else come from the
#     AppDir. Only the libraries an ELF of the AppDir needs directly are judged (the
#     host's own libraries bring their dependencies);
# (c) an ELF has an absolute RUNPATH/RPATH entry (only $ORIGIN-relative ones may ship),
#     or a .pth file or sitecustomize.py refers to the host's Python
#     (/usr/lib64/python*, /usr/lib/python*), or a symbolic link is absolute;
# (d) AppRun is not byte-identical to build/appimage/AppRun, or apprun-hooks/ or
#     AppRun.wrapped exist (linuxdeploy's wrapper, whose hooks change the environment:
#     AppRun may change LD_LIBRARY_PATH only, see there).
set -euo pipefail

APPRUN="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build/appimage/AppRun"

(($# == 2)) || { echo "usage: $0 <AppDir> <max-glibc>" >&2; exit 2; }
APPDIR="$(cd "$1" && pwd)"
MAX_GLIBC="$2"
[[ $MAX_GLIBC =~ ^[0-9]+\.[0-9]+$ ]] || { echo "$0: bad glibc version '$MAX_GLIBC'" >&2; exit 2; }

problems=0
problem() {
    echo "check-appdir: $*" >&2
    problems=$((problems + 1))
}

# sonames the host provides (shell patterns)
HOST_LIBS=(
    'ld-linux-x86-64.so.*' 'libc.so.*' 'libm.so.*' 'libmvec.so.*' 'libpthread.so.*'
    'libdl.so.*' 'librt.so.*' 'libresolv.so.*' 'libutil.so.*' 'libanl.so.*'
    'libnss_*' 'libBrokenLocale.so.*' 'libthread_db.so.*' 'libc_malloc_debug.so.*'
    'libGL*' 'libEGL*' 'libOpenGL*' 'libvulkan*' 'libgbm*' 'libdrm*'
    'libX11*' 'libxcb*' 'libwayland-*' 'libxkbcommon*'
    'libfontconfig*' 'libfreetype*' 'libdbus-1*'
)
host_lib() {
    local pattern
    for pattern in "${HOST_LIBS[@]}"; do
        # shellcheck disable=SC2053
        [[ $1 == $pattern ]] && return 0
    done
    return 1
}

is_elf() { [[ -f $1 && ! -L $1 && $(head -c 4 "$1" | od -An -c | tr -d ' ') == '177ELF' ]]; }

version_gt() { [[ $1 != "$2" && $(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n 1) == "$1" ]]; }

elfs=0
while IFS= read -r -d '' f; do
    is_elf "$f" || continue
    elfs=$((elfs + 1))
    rel="${f#"$APPDIR"/}"
    dynamic="$(LC_ALL=C readelf -d "$f" 2>/dev/null || true)"

    # (a) glibc
    glibc="$(LC_ALL=C objdump -T "$f" 2>/dev/null | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' |
        sed 's/^GLIBC_//' | sort -uV | tail -n 1 || true)"
    if [[ -n $glibc ]] && version_gt "$glibc" "$MAX_GLIBC"; then
        problem "$rel needs GLIBC_$glibc (above $MAX_GLIBC)"
    fi

    # (c) RUNPATH/RPATH
    while IFS= read -r path; do
        IFS=: read -r -a entries <<<"$path"
        for e in "${entries[@]}"; do
            [[ $e == '$ORIGIN'* || $e == '${ORIGIN}'* ]] ||
                problem "$rel has the RUNPATH/RPATH entry '$e' (only \$ORIGIN-relative ones may ship)"
        done
    done < <(sed -n 's/.*(\(RUNPATH\|RPATH\)).*\[\(.*\)\]$/\2/p' <<<"$dynamic")

    # (b) libraries, with no environment
    needed="$(sed -n 's/.*(NEEDED).*\[\(.*\)\]$/\1/p' <<<"$dynamic")"
    [[ -n $needed ]] || continue
    ldd_out="$(cd / && env -i PATH=/usr/bin:/bin ldd "$f" 2>&1 || true)"
    if grep -q 'not found' <<<"$ldd_out"; then
        problem "$rel: $(grep 'not found' <<<"$ldd_out" | sed 's/^[[:space:]]*//' | paste -sd, -)"
    fi
    while IFS= read -r soname; do
        resolved="$(sed -n "s|^[[:space:]]*${soname//./\\.} => \(/[^ ]*\) (.*|\1|p" <<<"$ldd_out" | head -n 1)"
        [[ -n $resolved ]] || continue  # not found: reported above
        real="$(readlink -f "$resolved")"
        if [[ $real != "$APPDIR"/* ]] && ! host_lib "$soname"; then
            problem "$rel needs $soname, which comes from the host ($resolved)"
        fi
    done <<<"$needed"
done < <(find "$APPDIR" -type f -print0)

# (c) Python configuration that would bring the host's Python in
while IFS= read -r -d '' f; do
    if grep -nE '/usr/lib(64)?/python' "$f" >/dev/null; then
        problem "${f#"$APPDIR"/} refers to the host's Python: $(grep -hE '/usr/lib(64)?/python' "$f" | head -n 1)"
    fi
done < <(find "$APPDIR" -type f \( -name '*.pth' -o -name 'sitecustomize.py' \) -print0)

# (c) absolute symbolic links point to the host
while IFS= read -r -d '' l; do
    target="$(readlink "$l")"
    [[ $target == /* ]] && problem "${l#"$APPDIR"/} is an absolute link to $target"
done < <(find "$APPDIR" -type l -print0)

# (d) the AppRun
# (sha256sum: coreutils, unlike cmp, is in every container that builds this)
if [[ ! -f $APPDIR/AppRun || $(sha256sum <"$APPRUN") != "$(sha256sum <"$APPDIR/AppRun")" ]]; then
    problem "AppRun is not build/appimage/AppRun"
fi
[[ -x $APPDIR/AppRun ]] || problem "AppRun is not executable"
for wrapper in apprun-hooks AppRun.wrapped; do
    [[ ! -e $APPDIR/$wrapper && ! -L $APPDIR/$wrapper ]] ||
        problem "$wrapper exists (the AppRun wrapper of linuxdeploy)"
done

if ((problems > 0)); then
    echo "check-appdir: FAILED, $problems problem(s) in $elfs ELF files of $APPDIR" >&2
    exit 1
fi
echo "check-appdir: $APPDIR passed ($elfs ELF files, glibc <= $MAX_GLIBC)"
