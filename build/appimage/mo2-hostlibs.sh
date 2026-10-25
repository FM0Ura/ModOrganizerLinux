# The choice between the bundled and the host's libstdc++ (spec of Phase 5, section
# 3.3), sourced (bash) by the AppRun and by the QtWebEngineProcess wrapper of the
# AppImage, which are installed as AppDir/AppRun and AppDir/usr/libexec/QtWebEngineProcess;
# this file is AppDir/usr/libexec/mo2-hostlibs.sh.
#
# The programs of the AppDir find their libraries through their RUNPATH, including the
# bundled libstdc++.so.6 and libgcc_s.so.1 in usr/lib (built with the GCC of Fedora 44).
# A host with a newer libstdc++ (one with a higher GLIBCXX_3.4.N) must use its own
# instead: host libraries loaded into the process (the OpenGL and Vulkan drivers) may
# need it.

# highest N of the GLIBCXX_3.4.N version strings of a libstdc++, or nothing
mo2_glibcxx_level() {
    grep -ao 'GLIBCXX_3\.4\.[0-9]*' "$1" 2>/dev/null |
        sed 's/^GLIBCXX_3\.4\.//' | sort -n | tail -n 1
}

# the path of the first x86-64 entry of a library in the cache of the dynamic linker
mo2_host_library() {
    local ldconfig candidate
    ldconfig="$(command -v ldconfig || true)"
    for candidate in /sbin/ldconfig /usr/sbin/ldconfig; do
        [[ -n $ldconfig ]] && break
        [[ -x $candidate ]] && ldconfig="$candidate"
    done
    [[ -n $ldconfig ]] || return 0

    "$ldconfig" -p 2>/dev/null |
        sed -n "s/^[[:space:]]*$1 (libc6,x86-64[^)]*) => //p" | head -n 1
}

# mo2_hostlibs_dir <AppDir>: when the host's libstdc++ is newer than the one in
# <AppDir>/usr/lib, makes a directory with links to the host's libstdc++.so.6 and
# libgcc_s.so.1 and prints it, for the front of LD_LIBRARY_PATH (which wins over
# RUNPATH); fails otherwise, and then nothing must change
mo2_hostlibs_dir() {
    local bundled host hostgcc
    bundled="$(mo2_glibcxx_level "$1/usr/lib/libstdc++.so.6")"
    host="$(mo2_host_library 'libstdc++\.so\.6')"
    hostgcc="$(mo2_host_library 'libgcc_s\.so\.1')"
    [[ -n $bundled && -n $host && -n $hostgcc && -r $host && -r $hostgcc ]] || return 1

    local level
    level="$(mo2_glibcxx_level "$host")"
    [[ -n $level ]] && ((level > bundled)) || return 1

    # a directory of this user only; anything else in its place is not used
    local dir="${XDG_RUNTIME_DIR:-/tmp}/mo2-hostlibs-$(id -u)"
    mkdir -p -m 700 "$dir" 2>/dev/null
    [[ -d $dir && ! -L $dir && -O $dir ]] || return 1
    ln -sfn "$host" "$dir/libstdc++.so.6" &&
        ln -sfn "$hostgcc" "$dir/libgcc_s.so.1" || return 1

    printf '%s\n' "$dir"
}
