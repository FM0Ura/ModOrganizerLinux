# Phase 2 VFS performance

Mount, recursive readdir, open+read+close and update of the FUSE view of a synthetic
instance, measured by `benchmarks/run-vfs.sh` (`mo2-vfs/src/bin/mo2-vfs-bench.rs`).
Each run appends a section below.

## 2026-10-15

- superproject commit: `a33f468`; build: cargo release profile
- machine: AMD Ryzen 5 5600G with Radeon Graphics (12 logical CPUs); kernel: 7.2.4-ogc3.1.fc44.x86_64; temp dir filesystem: overlay
- instance: 2000 mods, 200000 files, seed 1

| Measure | Target (spec 8) | Median of 3 runs | Result |
|---|---|---|---|
| `mount` until `ready` | < 1000 ms | 11388.3 ms | NOT met |
| recursive `readdir`, warm cache | <= 2x native | 13699.5 ms vs 2514.4 ms native = 5.45x | NOT met |
| `open`+`read`+`close` of 10000 files, warm cache | <= 2x native | 1075.1 ms vs 171.5 ms native = 6.27x | NOT met |
| `update` with the same list | < 1000 ms | 13743.9 ms | NOT met |

- mods: 2000, entries in the merged tree: 320788, files: 178073, sample: 10000, per-run repetitions: 3
- mount_ms per run: 14118.1, 11388.3, 11193.7
- readdir_ms per run: 13699.5, 13689.5, 13722.5
- readdir_native_ms per run: 2514.4, 2507.3, 2524.6
- open_read_ms per run: 1086.1, 1074.2, 1075.1
- open_read_native_ms per run: 174.0, 171.5, 167.0
- update_ms per run: 13743.9, 13709.7, 13836.8

### Notes and probable causes (not profiled; Task 18 decides)

All four targets were missed in this run.

- Method: the instance lives in `/var/tmp` (overlay on NVMe, so not tmpfs); the mountpoint is
  the generated empty `data` directory with one directory mapping per mod, alphabetical order
  = priority. The "native" tree is a plain copy of the mounted view made by the bench through
  the view before timing (untimed). The first walk of each kind is an untimed warm-up, then
  3 timed repetitions; the median of those is the per-run value, and the table shows the
  median of the 3 runs. `update` uses the same mapping list and is the median of 3 calls.
  The sample is 10000 files drawn with a seeded splitmix64 (with replacement).
- `mount` (11.4 s) and `update` (13.7 s) are both dominated by building the merged tree from
  2000 layers (320788 entries) with one directory scan per layer, single-threaded, plus
  (for `update`) the kernel notification pass; both are about 11 to 14x over target.
- `readdir` (13.7 s for 320788 entries, about 43 us per entry versus 7.8 us native) looks
  CPU-bound in `do_readdir` (`mo2-vfs/src/fs.rs`): per entry it builds the full path
  (`path_of`), folds it and takes the global inode-table lock to find the inode, and each
  readdir page restarts the child iterator with `.skip(offset)`, which is quadratic in the
  size of big directories. The FUSE round trips (one per page) add to that.
- `open`+`read`+`close` (about 107 us per file versus 17 us native) pays for FUSE lookups,
  open and read round trips to the daemon on each file (a passthrough open is used when the
  kernel allows it); the 6x ratio is mostly round-trip latency, so the 2x target probably
  needs longer kernel attribute/entry timeouts and cheaper per-call work rather than a
  single fix.
