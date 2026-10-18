//! Programs started for the application (`spawn` requests).

use crate::session::{ErrorCode, SessionError};
use std::collections::BTreeMap;
use std::fs::OpenOptions;
use std::os::unix::process::{CommandExt, ExitStatusExt};
use std::path::Path;
use std::process::{Child, Command, ExitStatus, Stdio};

fn failed(message: String, path: &Path) -> SessionError {
    SessionError {
        code: ErrorCode::SpawnFailed,
        message,
        path: Some(path.to_path_buf()),
    }
}

/// Starts `argv` in `cwd`, in a process group of its own (its pgid is its pid), with the
/// environment of this process plus `env`. Its standard input is `/dev/null`; its output
/// and errors go to `output` (truncated) or to `/dev/null`, never to the protocol.
/// `argv[0]` is looked up in the `PATH` of the final environment. `argv` is not empty.
pub fn start(
    argv: &[String],
    env: &BTreeMap<String, String>,
    cwd: &Path,
    output: Option<&Path>,
) -> Result<Child, SessionError> {
    let (program, args) = argv.split_first().expect("argv is not empty");
    if !cwd.is_dir() {
        return Err(failed(
            format!("the working directory {} does not exist", cwd.display()),
            cwd,
        ));
    }
    let mut command = Command::new(program);
    command
        .args(args)
        .envs(env)
        .current_dir(cwd)
        .stdin(Stdio::null())
        .process_group(0);
    match output {
        Some(path) => {
            let file = OpenOptions::new()
                .create(true)
                .write(true)
                .truncate(true)
                .open(path)
                .map_err(|e| failed(format!("cannot open {}: {e}", path.display()), path))?;
            let err = file
                .try_clone()
                .map_err(|e| failed(format!("cannot open {}: {e}", path.display()), path))?;
            command.stdout(file).stderr(err);
        }
        None => {
            command.stdout(Stdio::null()).stderr(Stdio::null());
        }
    }
    command
        .spawn()
        .map_err(|e| failed(format!("cannot start {program}: {e}"), Path::new(program)))
}

/// The exit code of a program, or 128 + the signal that killed it.
pub fn exit_code(status: ExitStatus) -> i32 {
    status
        .code()
        .unwrap_or_else(|| 128 + status.signal().unwrap_or(0))
}
