#!/usr/bin/env bash
# Phase 1 refresh benchmark: generate a synthetic instance, build and run
# organizer_bench_refresh from the application fork, append the result to
# benchmarks/results/2026-10-phase1-baseline.md.
#
#   benchmarks/run.sh [--mods N] [--files N] [--seed N] [--results FILE] [--no-record]
#
# Defaults: 2000 mods, 200000 files, seed 1. The instance is generated in a temporary
# directory (BENCH_TMPDIR, default $TMPDIR or /tmp) and removed afterwards; when the
# disk has too little free space the size is scaled down (mods and files by the same
# factor) and the size actually used is recorded in the result.
#
# Environment: MO2_SRC (default ../modorganizer) is the application fork; the build
# needs what build.sh needs (CMAKE_PREFIX_PATH etc., e.g. after
# `source /opt/mo2env/activate-mo2.sh`), and ./build.sh must have installed the
# dependencies into ./install first.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
MO2_SRC="${MO2_SRC:-$ROOT/../modorganizer}"
INSTALL="$ROOT/install"
BUILD_DIR="$ROOT/build-out/bench"
RESULTS="$HERE/results/2026-10-phase1-baseline.md"

MODS=2000
FILES=200000
SEED=1
RECORD=1

die() { echo "run.sh: $*" >&2; exit 1; }
log() { echo "==> $*"; }

while [[ $# -gt 0 ]]; do
    case $1 in
        --mods) MODS="$2"; shift 2 ;;
        --files) FILES="$2"; shift 2 ;;
        --seed) SEED="$2"; shift 2 ;;
        --results) RESULTS="$2"; shift 2 ;;
        --no-record) RECORD=0; shift ;;
        -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
done

if ! command -v cmake >/dev/null && [[ -f /opt/mo2env/activate-mo2.sh ]]; then
    # shellcheck disable=SC1091
    source /opt/mo2env/activate-mo2.sh
fi
[[ -d $MO2_SRC ]] || die "application source not found: $MO2_SRC"
[[ -d $INSTALL ]] || die "$INSTALL not found, run ./build.sh first"
export CMAKE_PREFIX_PATH="$INSTALL${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"

# --- build ---------------------------------------------------------------------------
log "configure and build organizer_bench_refresh"
cmake -S "$MO2_SRC" -B "$BUILD_DIR" --preset linux-gcc -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX="$INSTALL" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH//:/;}" \
    -DBUILD_TESTING=OFF -DMO2_BUILD_BENCHMARKS=ON >/dev/null
cmake --build "$BUILD_DIR" --target organizer_bench_refresh
BENCH="$BUILD_DIR/organizer_bench_refresh"
[[ -x $BENCH ]] || BENCH="$(find "$BUILD_DIR" -name organizer_bench_refresh -type f -perm -u+x | head -1)"
[[ -x $BENCH ]] || die "organizer_bench_refresh was not built"

# --- size ----------------------------------------------------------------------------
TMPBASE="${BENCH_TMPDIR:-${TMPDIR:-/tmp}}"
mkdir -p "$TMPBASE"
avail_kib=$(df -Pk "$TMPBASE" | awk 'NR==2 {print $4}')
# per file: a 4 KiB block (files are 0-4 KiB, so 2 KiB of data on average, but rounded
# up to blocks), a directory entry and an inode; keep a margin of 25% and 1 GiB
need_kib() { echo $(( $1 * 6 * 5 / 4 + 1048576 )); }
REQ_MODS=$MODS REQ_FILES=$FILES
while (( $(need_kib "$FILES") > avail_kib )) && (( FILES > MODS )); do
    FILES=$(( FILES * 9 / 10 ))
    MODS=$(( MODS * 9 / 10 ))
    (( MODS < 1 )) && MODS=1
    (( FILES < MODS )) && FILES=$MODS
done
(( $(need_kib "$FILES") <= avail_kib )) || die "not enough free space in $TMPBASE"
SCALED=""
if (( MODS != REQ_MODS || FILES != REQ_FILES )); then
    SCALED="scaled down from $REQ_MODS mods / $REQ_FILES files (free space in $TMPBASE: $((avail_kib / 1024)) MiB)"
    log "$SCALED"
fi

WORK="$(mktemp -d "$TMPBASE/mo2-bench.XXXXXX")"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

# --- generate ------------------------------------------------------------------------
log "generate $MODS mods / $FILES files (seed $SEED) in $WORK"
t0=$(date +%s)
python3 "$HERE/gen_instance.py" --mods "$MODS" --files "$FILES" --seed "$SEED" "$WORK/instance"
log "generated in $(( $(date +%s) - t0 )) s"

# the first run of the benchmark is untimed-for-the-metric; to make it a cold-cache run
# drop the page cache when we are allowed to
CACHE_NOTE="page cache not dropped (the instance was just written, so the first run is warm)"
sync
if [[ -w /proc/sys/vm/drop_caches ]] && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null; then
    CACHE_NOTE="page cache dropped before the first run"
fi
log "$CACHE_NOTE"

# --- run -----------------------------------------------------------------------------
log "run"
OUT="$("$BENCH" "$WORK/instance" 5)"
echo "$OUT"

metric() { sed -n "s/^$1=//p" <<<"$OUT"; }
MEDIAN="$(metric refresh_ms_median)"
RSS="$(metric peak_rss_kib)"
[[ -n $MEDIAN && -n $RSS ]] || die "the benchmark did not print its metrics"

# --- record --------------------------------------------------------------------------
if (( RECORD )); then
    DATE="$(date -u +%Y-%m-%d)"
    CPU="$(sed -n 's/^model name[[:space:]]*: //p' /proc/cpuinfo | head -1)"
    CORES="$(nproc)"
    KERNEL="$(uname -sr)"
    SUPER="$(git -C "$ROOT" rev-parse --short HEAD)$(git -C "$ROOT" diff --quiet HEAD 2>/dev/null || echo '+dirty')"
    FORK="$(git -C "$MO2_SRC" rev-parse --short HEAD)$(git -C "$MO2_SRC" diff --quiet HEAD 2>/dev/null || echo '+dirty')"
    mkdir -p "$(dirname "$RESULTS")"
    if [[ ! -s $RESULTS ]]; then
        cat > "$RESULTS" <<'HDR'
# Phase 1 performance baseline

Refresh of the data tree (`DirectoryEntry` built from every mod of a synthetic instance),
measured by `benchmarks/run.sh`. Each run appends a section below.
HDR
    fi
    {
        echo
        echo "## $DATE"
        echo
        echo "- superproject commit: \`$SUPER\`; fork (modorganizer) commit: \`$FORK\`"
        echo "- CPU: $CPU ($CORES logical CPUs); kernel: $KERNEL"
        echo "- size: $MODS mods, $FILES files, seed $SEED, $(metric threads) refresh threads${SCALED:+ ($SCALED)}"
        echo "- build: RelWithDebInfo, $(basename "${CXX:-c++}")"
        echo "- **refresh_ms_median = $MEDIAN** (5 timed repetitions after one warm-up: $(metric refresh_ms_all) ms)"
        echo "- **peak_rss_kib = $RSS**"
        echo "- first run (before the timed ones): $(metric first_run_ms) ms; $CACHE_NOTE"
        echo "- files registered: $(metric files_registered)"
    } >> "$RESULTS"
    log "appended to $RESULTS"
fi
