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

- Method: the instance lives in `/var/tmp` of the distrobox (an overlay filesystem, which
  flatters the native baseline; use `--tmpdir` to place it on a real filesystem); the mountpoint is
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
  open and read round trips to the daemon on each file (passthrough was off: the bench mounts with
  `VfsOptions::default()`, `try_passthrough: false`); the 6x ratio is mostly round-trip latency, so the 2x target probably
  needs longer kernel attribute/entry timeouts and cheaper per-call work rather than a
  single fix.

## 2026-10-17

- superproject commit: `f73e99f`; build: cargo release profile
- machine: AMD Ryzen 5 5600G with Radeon Graphics (12 logical CPUs); kernel: 7.2.4-ogc3.1.fc44.x86_64; temp dir filesystem: btrfs
- instance: 2000 mods, 200000 files, seed 1

| Measure | Target (spec 8) | Median of 3 runs | Result |
|---|---|---|---|
| `mount` until `ready` | < 1000 ms | 915.8 ms | met |
| recursive `readdir`, warm cache | <= 2x native | 1666.0 ms vs 1989.0 ms native = 0.84x | met |
| `open`+`read`+`close` of 10000 files, warm cache | <= 2x native | 386.0 ms vs 114.9 ms native = 3.36x | NOT met |
| `update` with the same list | < 1000 ms | 906.0 ms | met |

- mods: 2000, entries in the merged tree: 320788, files: 178073, sample: 10000, per-run repetitions: 3
- mount_ms per run: 2972.0, 915.8, 858.8
- readdir_ms per run: 1657.2, 1666.0, 1697.8
- readdir_native_ms per run: 1939.6, 1989.0, 1996.5
- open_read_ms per run: 386.0, 386.4, 385.2
- open_read_native_ms per run: 113.9, 116.8, 114.9
- update_ms per run: 873.6, 975.2, 906.0

### Notes (after Task 9b, the optimization pass)

Before -> after (medians, 2000 mods; before = 2026-10-15 above, on the container overlay;
the 500-mod profile instance on btrfs had the same ratios before: readdir 6.7x, open+read 8.4x):

| Measure | Before | After |
|---|---|---|
| `mount` | 11388 ms | 916 ms (cold first run: 2972 ms) |
| recursive `readdir` | 5.45x native | 0.84x native |
| `open`+`read`+`close` | 6.27x native | 3.36x native |
| `update` | 13744 ms | 906 ms |

The < 1 s targets for `mount` and `update` are met with a warm metadata cache (the layers'
dentries and inodes in the kernel's cache); with a cold one the first mount took 2972 ms
(see below). `update` runs right after a mount and walk, so its runs are always warm.

- Temp dir on btrfs (`--tmpdir $HOME/.cache/mo2-vfs-bench-tmp`), no longer the overlay.
- `mount` / `update`: the layers are scanned in parallel (12 threads) and merged in priority
  order. Both now take about the time of the scan, which is kernel-bound (about 5 syscalls per
  scanned directory; this instance has 488607 directories in its mods) plus about 90 ms to
  build the child maps. They need the layers' metadata in the kernel's dentry/inode cache:
  the first run (2972 ms) came right after generating the instance, and a later fresh process
  after the machine sat idle measured 2979 ms, then 880 ms again; with a cold metadata cache
  the scan reads the disk.
- `readdir`: the kernel now caches listings (no opendir), so a warm walk sends no requests to
  the daemon; the cached FUSE listing is served from the page cache, which is a little faster
  than btrfs's own readdir on the native copy.
- `open`+`read`+`close`: NOT met. The page cache is kept across opens and close sends no flush,
  so a warm file costs one OPEN round trip to the daemon (plus an asynchronous RELEASE):
  about 39 us per file against 11 us native. The round trip itself is the cost (the handler is
  an openat and an fstat; 1, 2, 4 or 8 FUSE threads give the same result). With
  `--passthrough` the numbers are the same (387.5 ms; 389.8 ms on a second run): without
  CAP_SYS_ADMIN the kernel refuses the backing file and the VFS reads with pread. Meeting 2x
  needs passthrough (privileged) or a lower-latency FUSE transport (io_uring), not
  available here.
