#!/usr/bin/env bash
# Phase 2 VFS benchmark: generate a synthetic instance, run mo2-vfs-bench 3 times and
# append the medians (against the spec 8 targets) to benchmarks/results/2026-10-phase2-vfs.md.
#
#   benchmarks/run-vfs.sh [--mods N] [--files N] [--seed N] [--runs N] [--results FILE] [--no-record]
#
# Defaults: 2000 mods, 200000 files, seed 1, 3 runs. The instance is generated in a
# temporary directory (BENCH_TMPDIR, default $TMPDIR or /tmp) and removed afterwards; when
# the disk has too little free space the size is scaled down (mods and files by the same
# factor) and the size actually used is recorded. The merged tree is also copied once to a
# plain directory (the "native" comparison), so about twice the instance size is needed.
#
# Needs cargo, python3, /dev/fuse and fusermount3 (on Bazzite: inside the distrobox,
#   distrobox enter mo2-run -- bash -lc 'cd <repo> && benchmarks/run-vfs.sh').
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
RESULTS="$HERE/results/2026-10-phase2-vfs.md"

MODS=2000
FILES=200000
SEED=1
RUNS=3
RECORD=1

die() { echo "run-vfs.sh: $*" >&2; exit 1; }
log() { echo "==> $*"; }

while [[ $# -gt 0 ]]; do
    case $1 in
        --mods) MODS="$2"; shift 2 ;;
        --files) FILES="$2"; shift 2 ;;
        --seed) SEED="$2"; shift 2 ;;
        --runs) RUNS="$2"; shift 2 ;;
        --results) RESULTS="$2"; shift 2 ;;
        --no-record) RECORD=0; shift ;;
        -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
done

command -v cargo >/dev/null || die "cargo not found (on Bazzite run this inside the distrobox)"
command -v python3 >/dev/null || die "python3 not found"
[[ -e /dev/fuse ]] || die "/dev/fuse not found"
command -v fusermount3 >/dev/null || die "fusermount3 not found"

log "build mo2-vfs-bench (release)"
(cd "$ROOT/mo2-vfs" && cargo build --release --bin mo2-vfs-bench)
BENCH="$(cd "$ROOT/mo2-vfs" && cargo metadata --format-version 1 --no-deps | python3 -c 'import json,sys; print(json.load(sys.stdin)["target_directory"])')/release/mo2-vfs-bench"
[[ -x $BENCH ]] || die "mo2-vfs-bench was not built"

# --- size ----------------------------------------------------------------------------
TMPBASE="${BENCH_TMPDIR:-${TMPDIR:-/tmp}}"
mkdir -p "$TMPBASE"
avail_kib=$(df -Pk "$TMPBASE" | awk 'NR==2 {print $4}')
FSTYPE=$(df -PT "$TMPBASE" | awk 'NR==2 {print $2}')
# the instance and its native copy: per file a 4 KiB block each, plus entries; 25% margin and 1 GiB
need_kib() { echo $(( $1 * 12 * 5 / 4 + 1048576 )); }
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

WORK="$(mktemp -d "$TMPBASE/mo2-vfs-bench.XXXXXX")"
cleanup() {
    findmnt -rn -t fuse.mo2-vfs -o TARGET | grep -F "$WORK" | while read -r m; do fusermount3 -u -z "$m" || true; done
    rm -rf "$WORK"
}
trap cleanup EXIT

log "generate $MODS mods / $FILES files (seed $SEED) in $WORK"
python3 "$HERE/gen_instance.py" --mods "$MODS" --files "$FILES" --seed "$SEED" "$WORK/instance"

# --- run -----------------------------------------------------------------------------
declare -a OUTS=()
for ((i = 1; i <= RUNS; i++)); do
    log "run $i/$RUNS"
    rm -rf "$WORK/native"
    OUT="$(TMPDIR="$WORK" "$BENCH" --data "$WORK/instance/data" --mods "$WORK/instance/mods" \
        --files-sample 10000 --seed "$SEED" --native "$WORK/native" --json)"
    echo "$OUT"
    OUTS+=("$OUT")
done
rm -rf "$WORK/native"

# medians of every metric, the checks against the spec 8 targets, and the markdown section
SECTION="$(printf '%s\n' "${OUTS[@]}" | python3 -c '
import json, statistics, sys
runs = [json.loads(l) for l in sys.stdin if l.strip()]
def med(k): return statistics.median(r[k] for r in runs)
m = {k: med(k) for k in ["mount_ms", "readdir_ms", "readdir_native_ms", "open_read_ms", "open_read_native_ms", "update_ms"]}
rd = m["readdir_ms"] / m["readdir_native_ms"]
orr = m["open_read_ms"] / m["open_read_native_ms"]
def ok(b): return "met" if b else "NOT met"
rows = [
    ("`mount` until `ready`", "< 1000 ms", "%.1f ms" % m["mount_ms"], ok(m["mount_ms"] < 1000)),
    ("recursive `readdir`, warm cache", "<= 2x native", "%.1f ms vs %.1f ms native = %.2fx" % (m["readdir_ms"], m["readdir_native_ms"], rd), ok(rd <= 2)),
    ("`open`+`read`+`close` of %d files, warm cache" % runs[0]["sample"], "<= 2x native", "%.1f ms vs %.1f ms native = %.2fx" % (m["open_read_ms"], m["open_read_native_ms"], orr), ok(orr <= 2)),
    ("`update` with the same list", "< 1000 ms", "%.1f ms" % m["update_ms"], ok(m["update_ms"] < 1000)),
]
print("| Measure | Target (spec 8) | Median of %d runs | Result |" % len(runs))
print("|---|---|---|---|")
for r in rows: print("| " + " | ".join(r) + " |")
print()
print("- mods: %d, entries in the merged tree: %d, files: %d, sample: %d, per-run repetitions: %d" % (runs[0]["mods"], runs[0]["entries"], runs[0]["files"], runs[0]["sample"], runs[0]["reps"]))
for k in m: print("- %s per run: %s" % (k, ", ".join("%.1f" % r[k] for r in runs)))
')"

if (( RECORD )); then
    DATE="$(date +%F)"
    CPU="$(lscpu | sed -n 's/^Model name:[[:space:]]*//p' | head -1)"
    SUPER="$(git -C "$ROOT" rev-parse --short HEAD)$(git -C "$ROOT" diff --quiet HEAD 2>/dev/null || echo '+dirty')"
    mkdir -p "$(dirname "$RESULTS")"
    if [[ ! -s $RESULTS ]]; then
        cat > "$RESULTS" <<'HDR'
# Phase 2 VFS performance

Mount, recursive readdir, open+read+close and update of the FUSE view of a synthetic
instance, measured by `benchmarks/run-vfs.sh` (`mo2-vfs/src/bin/mo2-vfs-bench.rs`).
Each run appends a section below.
HDR
    fi
    {
        echo
        echo "## $DATE"
        echo
        echo "- superproject commit: \`$SUPER\`; build: cargo release profile"
        echo "- machine: $CPU ($(nproc) logical CPUs); kernel: $(uname -r); temp dir filesystem: $FSTYPE"
        echo "- instance: $MODS mods, $FILES files, seed $SEED${SCALED:+ ($SCALED)}"
        echo
        echo "$SECTION"
    } >> "$RESULTS"
    log "appended to $RESULTS"
fi
