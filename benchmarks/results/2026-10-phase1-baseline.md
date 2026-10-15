# Phase 1 performance baseline

Refresh of the data tree (`DirectoryEntry` built from every mod of a synthetic instance),
measured by `benchmarks/run.sh`. Each run appends a section below.

## 2026-10-15

- superproject commit: `3f713d2b`; fork (modorganizer) commit: `e6e4b49` plus the uncommitted benchmark sources of the commit that adds this file (`bench: refresh benchmark and phase 1 baseline`)
- CPU: Intel(R) Xeon(R) Processor @ 2.10GHz (4 logical CPUs); kernel: Linux 6.18.44-fc-v114
- size: 2000 mods, 200000 files, seed 1, 10 refresh threads
- build: RelWithDebInfo, x86_64-conda-linux-gnu-g++
- **refresh_ms_median = 1580** (5 timed repetitions after one warm-up: 1562.0,1551.2,1585.8,1579.6,1587.5 ms)
- **peak_rss_kib = 339656**
- first run (before the timed ones): 10290.8 ms; page cache dropped before the first run
- files registered: 178073
