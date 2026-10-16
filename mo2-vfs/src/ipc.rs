//! Line-delimited JSON protocol: requests from the application, events back, and the
//! server state that turns one into the other.

use crate::layer::Skip;
use crate::mapping::Mapping;
use crate::session::{ErrorCode, Session, SessionError};
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
    },
    Unmounted,
    Status {
        roots: Vec<PathBuf>,
        nodes: usize,
        masks: usize,
        groups: Vec<i32>,
    },
}

impl Event {
    pub fn bad_request(message: impl Into<String>) -> Event {
        Event::Error {
            code: ErrorCode::BadRequest,
            message: message.into(),
            path: None,
        }
    }
}

impl From<SessionError> for Event {
    fn from(e: SessionError) -> Event {
        Event::Error {
            code: e.code,
            message: e.message,
            path: e.path,
        }
    }
}

/// Parses one input line; a line that is not a valid request becomes a `bad_request` event.
pub fn parse_request(line: &str) -> Result<Request, Event> {
    serde_json::from_str(line).map_err(|e| Event::bad_request(format!("invalid request: {e}")))
}

/// The mount (at most one per process) and the process groups watched on its behalf.
#[derive(Default)]
pub struct Server {
    session: Option<Session>,
    groups: Vec<i32>,
}

impl Server {
    pub fn new() -> Server {
        Server::default()
    }

    pub fn is_mounted(&self) -> bool {
        self.session.is_some()
    }

    pub fn has_groups(&self) -> bool {
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

    /// Unmounts (if mounted) and forgets the watched groups.
    pub fn unmount(&mut self) -> Result<(), SessionError> {
        self.groups.clear();
        match self.session.take() {
            Some(session) => session.unmount(),
            None => Ok(()),
        }
    }

    /// Drops the groups that ended. When groups were watched and none is left, unmounts and
    /// returns the spontaneous `unmounted` event.
    pub fn poll_groups(&mut self) -> Option<Event> {
        if self.groups.is_empty() {
            return None;
        }
        self.groups.retain(|&g| group_alive(g));
        if !self.groups.is_empty() {
            return None;
        }
        tracing::info!("every watched process group has ended; unmounting");
        if let Err(e) = self.unmount() {
            tracing::error!(error = %e, "automatic unmount failed");
        }
        Some(Event::Unmounted)
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
    }
}
