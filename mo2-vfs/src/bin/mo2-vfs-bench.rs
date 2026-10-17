//! Benchmark of the virtual file system.
//!
//!     mo2-vfs-bench --data <empty dir> --mods <dir with the mod directories>
//!                   [--files-sample 10000] [--seed 1] [--reps 3] [--native <dir>] [--json]
//!                   [--passthrough]
//!
//! Mounts one directory mapping per mod (alphabetical order is the priority, the last
//! wins) on `--data` and measures, in milliseconds: `mount` (until `Session::mount_in`
//! returns), a full recursive `readdir` of the mounted tree, `open`+`read`+`close` of a
//! seeded sample of files, and `update` with the same mapping list. The "native" values
//! are the same two walks over a plain copy of the merged tree. The copy is made by this
//! program through the mounted view (so it holds exactly the merged content) before the
//! timed walks and is not part of any timing. Each walk runs once untimed (warm-up) and
//! `--reps` times timed; the median is reported. `--passthrough` mounts with
//! `try_passthrough` (without the privilege the kernel needs it falls back to `pread`).

use mo2_vfs::fs::VfsOptions;
use mo2_vfs::layer::Skip;
use mo2_vfs::mapping::Mapping;
use mo2_vfs::session::Session;
use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::time::Instant;

struct Args {
    data: PathBuf,
    mods: PathBuf,
    sample: usize,
    seed: u64,
    reps: usize,
    native: Option<PathBuf>,
    json: bool,
    passthrough: bool,
}

fn parse_args() -> Result<Args, String> {
    let mut a = Args {
        data: PathBuf::new(),
        mods: PathBuf::new(),
        sample: 10_000,
        seed: 1,
        reps: 3,
        native: None,
        json: false,
        passthrough: false,
    };
    let mut it = std::env::args().skip(1);
    while let Some(arg) = it.next() {
        let mut value = |name: &str| it.next().ok_or(format!("{name} needs a value"));
        match arg.as_str() {
            "--data" => a.data = value("--data")?.into(),
            "--mods" => a.mods = value("--mods")?.into(),
            "--native" => a.native = Some(value("--native")?.into()),
            "--files-sample" => {
                a.sample = value("--files-sample")?
                    .parse()
                    .map_err(|e| format!("{e}"))?
            }
            "--seed" => a.seed = value("--seed")?.parse().map_err(|e| format!("{e}"))?,
            "--reps" => a.reps = value("--reps")?.parse().map_err(|e| format!("{e}"))?,
            "--json" => a.json = true,
            "--passthrough" => a.passthrough = true,
            other => return Err(format!("unknown argument: {other}")),
        }
    }
    if a.data.as_os_str().is_empty() || a.mods.as_os_str().is_empty() {
        return Err("--data and --mods are required".into());
    }
    if a.reps == 0 {
        return Err("--reps must be at least 1".into());
    }
    Ok(a)
}

/// splitmix64: tiny deterministic generator for the seeded sample.
struct SplitMix(u64);

impl SplitMix {
    fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }
}

/// Recursive listing; returns the number of entries and, when asked, the relative paths of
/// the regular files.
fn walk(dir: &Path, rel: &Path, files: &mut Option<&mut Vec<PathBuf>>) -> std::io::Result<usize> {
    let mut count = 0;
    for entry in fs::read_dir(dir)? {
        let entry = entry?;
        count += 1;
        let ty = entry.file_type()?;
        let child = rel.join(entry.file_name());
        if ty.is_dir() {
            count += walk(&entry.path(), &child, files)?;
        } else if let Some(list) = files.as_deref_mut() {
            list.push(child);
        }
    }
    Ok(count)
}

fn copy_tree(from: &Path, to: &Path) -> std::io::Result<()> {
    fs::create_dir_all(to)?;
    for entry in fs::read_dir(from)? {
        let entry = entry?;
        let dest = to.join(entry.file_name());
        if entry.file_type()?.is_dir() {
            copy_tree(&entry.path(), &dest)?;
        } else {
            fs::copy(entry.path(), dest)?;
        }
    }
    Ok(())
}

fn read_sample(root: &Path, sample: &[PathBuf]) -> std::io::Result<u64> {
    let mut total = 0u64;
    let mut buf = Vec::with_capacity(8192);
    for rel in sample {
        buf.clear();
        let mut f = fs::File::open(root.join(rel))?;
        f.read_to_end(&mut buf)?;
        total += buf.len() as u64;
    }
    Ok(total)
}

fn median(mut v: Vec<f64>) -> f64 {
    v.sort_by(|a, b| a.partial_cmp(b).unwrap());
    v[v.len() / 2]
}

/// One warm-up run, then `reps` timed runs; the median in milliseconds.
fn timed<T>(reps: usize, mut f: impl FnMut() -> std::io::Result<T>) -> std::io::Result<f64> {
    f()?;
    let mut times = Vec::new();
    for _ in 0..reps {
        let t = Instant::now();
        f()?;
        times.push(t.elapsed().as_secs_f64() * 1000.0);
    }
    Ok(median(times))
}

fn run(a: &Args) -> Result<(), String> {
    let io = |what: &'static str| move |e: std::io::Error| format!("{what}: {e}");
    let data = a.data.canonicalize().map_err(io("--data"))?;
    let mut mod_dirs: Vec<PathBuf> = fs::read_dir(&a.mods)
        .map_err(io("--mods"))?
        .filter_map(|e| e.ok())
        .filter(|e| e.file_type().map(|t| t.is_dir()).unwrap_or(false))
        .filter_map(|e| match e.path().canonicalize() {
            Ok(p) => Some(p),
            Err(err) => {
                eprintln!("mo2-vfs-bench: skipping {}: {err}", e.path().display());
                None
            }
        })
        .collect();
    mod_dirs.sort();
    let mappings: Vec<Mapping> = mod_dirs
        .iter()
        .map(|m| Mapping {
            source: m.clone(),
            destination: data.clone(),
            is_directory: true,
            create_target: false,
        })
        .collect();

    let state = tempfile_dir("mo2-vfs-bench-state")?;
    let native = match &a.native {
        Some(p) => p.clone(),
        None => tempfile_dir("mo2-vfs-bench-native")?,
    };

    let t = Instant::now();
    let options = VfsOptions {
        try_passthrough: a.passthrough,
        ..VfsOptions::default()
    };
    let mut session = Session::mount_with(&state, &mappings, Skip::default(), options)
        .map_err(|e| format!("mount: {e}"))?;
    let mount_ms = t.elapsed().as_secs_f64() * 1000.0;

    let result = (|| -> Result<serde_json::Value, String> {
        // Native comparison tree: the merged view copied to a plain directory, untimed.
        copy_tree(&data, &native).map_err(io("copy of the merged view"))?;

        // Flush the freshly written copy so writeback does not disturb the timed walks.
        let _ = std::process::Command::new("sync").status();

        let mut files = Vec::new();
        let entries = walk(&native, Path::new(""), &mut Some(&mut files)).map_err(io("walk"))?;
        files.sort();
        let mut rng = SplitMix(a.seed);
        let n = a.sample.min(files.len());
        let sample: Vec<PathBuf> = (0..n)
            .map(|_| files[(rng.next() % files.len() as u64) as usize].clone())
            .collect();

        let readdir_ms =
            timed(a.reps, || walk(&data, Path::new(""), &mut None)).map_err(io("readdir"))?;
        let readdir_native_ms = timed(a.reps, || walk(&native, Path::new(""), &mut None))
            .map_err(io("readdir native"))?;
        let open_read_ms =
            timed(a.reps, || read_sample(&data, &sample)).map_err(io("open/read"))?;
        let open_read_native_ms =
            timed(a.reps, || read_sample(&native, &sample)).map_err(io("open/read native"))?;

        let mut update_times = Vec::new();
        for _ in 0..a.reps {
            let t = Instant::now();
            session
                .update(&mappings, Skip::default())
                .map_err(|e| format!("update: {e}"))?;
            update_times.push(t.elapsed().as_secs_f64() * 1000.0);
        }
        let update_ms = median(update_times);

        Ok(serde_json::json!({
            "mount_ms": mount_ms,
            "readdir_ms": readdir_ms,
            "readdir_native_ms": readdir_native_ms,
            "open_read_ms": open_read_ms,
            "open_read_native_ms": open_read_native_ms,
            "update_ms": update_ms,
            "mods": mod_dirs.len(),
            "entries": entries,
            "files": files.len(),
            "sample": n,
            "seed": a.seed,
            "reps": a.reps,
            "passthrough": a.passthrough,
        }))
    })();

    let unmounted = session.unmount().map_err(|e| format!("unmount: {e}"));
    if a.native.is_none() {
        let _ = fs::remove_dir_all(&native);
    }
    let _ = fs::remove_dir_all(&state);
    let value = result?;
    unmounted?;
    if a.json {
        println!("{value}");
    } else {
        println!("{}", serde_json::to_string_pretty(&value).unwrap());
    }
    Ok(())
}

/// A fresh private directory next to the mods directory's filesystem (TMPDIR otherwise).
fn tempfile_dir(prefix: &str) -> Result<PathBuf, String> {
    let base = std::env::var_os("TMPDIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/tmp"));
    for i in 0..1000u32 {
        let p = base.join(format!("{prefix}.{}.{i}", std::process::id()));
        match fs::create_dir(&p) {
            Ok(()) => return Ok(p),
            Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(e) => return Err(format!("{}: {e}", p.display())),
        }
    }
    Err("cannot create a temporary directory".into())
}

fn main() -> ExitCode {
    let args = match parse_args() {
        Ok(a) => a,
        Err(e) => {
            eprintln!("mo2-vfs-bench: {e}");
            return ExitCode::from(2);
        }
    };
    match run(&args) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("mo2-vfs-bench: {e}");
            ExitCode::FAILURE
        }
    }
}
