//! Line-delimited JSON protocol: requests from the application, events back, and the
//! server state that turns one into the other.

use crate::layer::Skip;
use crate::mapping::Mapping;
use crate::session::{ErrorCode, Session, SessionError, unmount_lazy};
use crate::state::{self, StateFile};
use crate::watch::group_alive;
use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Debug, Deserialize)]
#[serde(tag = "op", rename_all = "snake_case")]
pub enum Request {
    Mount {
        mappings: Vec<Mapping>,
        #[serde(default)]
        skip_file_suffixes: Vec<String>,
        #[serde(default)]
        skip_directories: Vec<String>,
    },
    Update {
        mappings: Vec<Mapping>,
        #[serde(default)]
        skip_file_suffixes: Vec<String>,
        #[serde(default)]
        skip_directories: Vec<String>,
    },
    Watch {
        pgid: i32,
    },
    Unmount,
    Status,
    Spawn {
        argv: Vec<String>,
        #[serde(default)]
        env: std::collections::BTreeMap<String, String>,
        cwd: PathBuf,
        #[serde(default)]
        wineprefix: Option<PathBuf>,
        #[serde(default)]
        output: Option<PathBuf>,
    },
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "event", rename_all = "snake_case")]
pub enum Event {
    Hello {
        version: String,
    },
    Ready {
        roots: Vec<PathBuf>,
    },
    Ok,
    Error {
        code: ErrorCode,
        message: String,
        path: Option<PathBuf>,
        /// Not the reply to a request: the automatic unmount (every watched group ended)
        /// failed and is retried; the mount is still there. Left out when false.
        #[serde(default, skip_serializing_if = "std::ops::Not::not")]
        spontaneous: bool,
    },
    Unmounted,
    Status {
        roots: Vec<PathBuf>,
        nodes: usize,
        masks: usize,
        groups: Vec<i32>,
    },
    Spawned {
        pid: i32,
    },
    Exited {
        pid: i32,
        code: i32,
    },
}

impl Event {
    pub fn bad_request(message: impl Into<String>) -> Event {
        Event::Error {
            code: ErrorCode::BadRequest,
            message: message.into(),
            path: None,
            spontaneous: false,
        }
    }
}

impl From<SessionError> for Event {
    fn from(e: SessionError) -> Event {
        Event::Error {
            code: e.code,
            message: e.message,
            path: e.path,
            spontaneous: false,
        }
    }
}

/// Parses one input line; a line that is not a valid request becomes a `bad_request` event.
pub fn parse_request(line: &str) -> Result<Request, Event> {
    serde_json::from_str(line).map_err(|e| Event::bad_request(format!("invalid request: {e}")))
}

/// The mount (at most one per process), the process groups watched on its behalf and the
/// programs started by `spawn` requests.
#[derive(Default)]
pub struct Server {
    session: Option<Session>,
    groups: Vec<i32>,
    children: Vec<std::process::Child>,
    /// Roots whose automatic unmount failed, retried lazily, with the directory of the
    /// state file that still lists them.
    stale: Vec<PathBuf>,
    stale_state_dir: Option<PathBuf>,
}

impl Server {
    pub fn new() -> Server {
        Server::default()
    }

    pub fn is_mounted(&self) -> bool {
        self.session.is_some()
    }

    /// True while something keeps the mount alive (watched process groups).
    pub fn has_watched(&self) -> bool {
        !self.groups.is_empty()
    }

    pub fn handle(&mut self, request: Request) -> Event {
        match request {
            Request::Mount {
                mappings,
                skip_file_suffixes,
                skip_directories,
            } => {
                if self.session.is_some() {
                    return Event::bad_request("already mounted; unmount first");
                }
                if let Err(e) = self.retry_stale() {
                    return e.into();
                }
                let skip = Skip::new(&skip_file_suffixes, &skip_directories);
                match Session::mount(&mappings, skip) {
                    Ok(session) => {
                        let roots = session.roots();
                        self.session = Some(session);
                        Event::Ready { roots }
                    }
                    Err(e) => e.into(),
                }
            }
            Request::Update {
                mappings,
                skip_file_suffixes,
                skip_directories,
            } => {
                let Some(session) = self.session.as_mut() else {
                    return Event::bad_request("not mounted");
                };
                let skip = Skip::new(&skip_file_suffixes, &skip_directories);
                match session.update(&mappings, skip) {
                    Ok(()) => Event::Ok,
                    Err(e) => e.into(),
                }
            }
            Request::Watch { pgid } => {
                if self.session.is_none() {
                    return Event::bad_request("not mounted");
                }
                if pgid <= 0 {
                    return Event::bad_request("pgid must be positive");
                }
                if !self.groups.contains(&pgid) {
                    self.groups.push(pgid);
                }
                Event::Ok
            }
            Request::Spawn {
                argv,
                env,
                cwd,
                wineprefix: _,
                output,
            } => {
                if argv.is_empty() {
                    return Event::bad_request("argv is empty");
                }
                match crate::spawn::start(&argv, &env, &cwd, output.as_deref()) {
                    Ok(child) => {
                        let pid = child.id() as i32;
                        tracing::info!(pid, ?argv, cwd = %cwd.display(), "started");
                        if self.session.is_some() && !self.groups.contains(&pid) {
                            // its process group (pgid == pid) keeps the mount alive
                            self.groups.push(pid);
                        }
                        self.children.push(child);
                        Event::Spawned { pid }
                    }
                    Err(e) => e.into(),
                }
            }
            Request::Unmount => match self.unmount() {
                Ok(()) => Event::Unmounted,
                Err(e) => e.into(),
            },
            Request::Status => {
                let (roots, stats) = match &self.session {
                    Some(s) => (s.roots(), s.stats()),
                    None => (Vec::new(), Default::default()),
                };
                Event::Status {
                    roots,
                    nodes: stats.nodes,
                    masks: stats.masks,
                    groups: self.groups.clone(),
                }
            }
        }
    }

    /// Reaps the started programs that ended: one `exited` event for each.
    pub fn poll_children(&mut self) -> Vec<Event> {
        let mut events = Vec::new();
        self.children.retain_mut(|child| match child.try_wait() {
            Ok(Some(status)) => {
                let code = crate::spawn::exit_code(status);
                tracing::info!(pid = child.id(), code, "program ended");
                events.push(Event::Exited {
                    pid: child.id() as i32,
                    code,
                });
                false
            }
            Ok(None) => true,
            Err(e) => {
                tracing::warn!(pid = child.id(), error = %e, "cannot wait for a started program");
                false
            }
        });
        events
    }

    /// Unmounts (if mounted) and forgets the watched groups; the roots of a failed
    /// automatic unmount are tried once more (lazily).
    pub fn unmount(&mut self) -> Result<(), SessionError> {
        self.groups.clear();
        let r = match self.session.take() {
            Some(session) => session.unmount(),
            None => Ok(()),
        };
        r.and(self.retry_stale())
    }

    /// Drops the groups that ended. When groups were watched and none is left, unmounts and
    /// returns the spontaneous `unmounted` event; if that unmount fails, a spontaneous `io`
    /// error instead (the mount is still there), and the unmount is retried lazily on the
    /// next polls until it succeeds, which is then reported by `unmounted`.
    pub fn poll_groups(&mut self) -> Option<Event> {
        if !self.stale.is_empty() {
            return match self.retry_stale() {
                Ok(()) => {
                    tracing::info!("the failed automatic unmount succeeded on retry");
                    Some(Event::Unmounted)
                }
                Err(_) => None,
            };
        }
        if self.groups.is_empty() {
            return None;
        }
        self.groups.retain(|&g| group_alive(g));
        if !self.groups.is_empty() {
            return None;
        }
        tracing::info!("every watched process group has ended; unmounting");
        let roots = self
            .session
            .as_ref()
            .map(Session::roots)
            .unwrap_or_default();
        let state_dir = self.session.as_ref().map(|s| s.state_dir().to_path_buf());
        let Err(e) = self.unmount() else {
            return Some(Event::Unmounted);
        };
        let mounted = state::mounted_mo2_vfs();
        self.stale = roots.into_iter().filter(|r| mounted.contains(r)).collect();
        if self.stale.is_empty() {
            tracing::warn!(error = %e, "automatic unmount reported an error, nothing is left mounted");
            return Some(Event::Unmounted);
        }
        self.stale_state_dir = state_dir;
        tracing::error!(error = %e, roots = ?self.stale, "automatic unmount failed; retrying");
        Some(Event::Error {
            code: ErrorCode::Io,
            message: format!("automatic unmount failed: {}", e.message),
            path: e.path.or_else(|| self.stale.first().cloned()),
            spontaneous: true,
        })
    }

    /// `fusermount3 -uz` on the roots of a failed automatic unmount that are still
    /// mounted; Ok once none is left (the state file that listed them is then removed).
    fn retry_stale(&mut self) -> Result<(), SessionError> {
        if self.stale.is_empty() {
            return Ok(());
        }
        let mounted = state::mounted_mo2_vfs();
        let mut first_err = None;
        self.stale.retain(|root| {
            if !mounted.contains(root) {
                return false;
            }
            match unmount_lazy(root) {
                Ok(()) => false,
                Err(m) => {
                    tracing::debug!(root = %root.display(), error = %m, "unmount retry failed");
                    first_err.get_or_insert(SessionError {
                        code: ErrorCode::Io,
                        message: format!(
                            "the previous mount on {} cannot be unmounted: {m}",
                            root.display()
                        ),
                        path: Some(root.clone()),
                    });
                    true
                }
            }
        });
        if self.stale.is_empty() {
            if let Some(dir) = self.stale_state_dir.take() {
                StateFile::remove_in(&dir, std::process::id());
            }
            return Ok(());
        }
        first_err.map_or(Ok(()), Err)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn requests_parse_with_defaults_and_unknown_fields() {
        let r = parse_request(r#"{"op":"mount","mappings":[],"extra":1}"#).unwrap();
        assert!(matches!(
            r,
            Request::Mount { ref skip_file_suffixes, ref skip_directories, .. }
                if skip_file_suffixes.is_empty() && skip_directories.is_empty()
        ));
        assert!(matches!(
            parse_request(r#"{"op":"watch","pgid":7}"#),
            Ok(Request::Watch { pgid: 7 })
        ));
        assert!(matches!(
            parse_request(r#"{"op":"spawn","argv":["x"],"cwd":"/"}"#),
            Ok(Request::Spawn { ref argv, ref env, ref wineprefix, ref output, .. })
                if argv == &["x"] && env.is_empty() && wineprefix.is_none() && output.is_none()
        ));
        assert!(matches!(
            parse_request(r#"{"op":"unmount"}"#),
            Ok(Request::Unmount)
        ));
        for bad in ["", "nope", r#"{"op":"x"}"#, r#"{"op":"watch"}"#, "[]"] {
            let Err(Event::Error { code, .. }) = parse_request(bad) else {
                panic!("{bad:?} should be rejected");
            };
            assert_eq!(code, ErrorCode::BadRequest);
        }
    }

    #[test]
    fn events_have_the_wire_shape() {
        let j = |e: &Event| serde_json::to_string(e).unwrap();
        assert_eq!(j(&Event::Ok), r#"{"event":"ok"}"#);
        assert_eq!(j(&Event::Unmounted), r#"{"event":"unmounted"}"#);
        assert_eq!(
            j(&Event::Hello {
                version: "1".into()
            }),
            r#"{"event":"hello","version":"1"}"#
        );
        assert_eq!(
            j(&Event::bad_request("m")),
            r#"{"event":"error","code":"bad_request","message":"m","path":null}"#
        );
        assert_eq!(
            j(&Event::Error {
                code: ErrorCode::Io,
                message: "m".into(),
                path: Some("/d".into()),
                spontaneous: true,
            }),
            r#"{"event":"error","code":"io","message":"m","path":"/d","spontaneous":true}"#
        );
    }
}
