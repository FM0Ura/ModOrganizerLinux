//! Process group tracking through `/proc`.

use std::fs;

/// The `(state, pgrp)` of a `/proc/<pid>/stat` line. The command name sits in parentheses
/// and may itself contain spaces and parentheses, so the fields are read after the last `)`.
fn parse_stat(stat: &str) -> Option<(char, i32)> {
    let rest = &stat[stat.rfind(')')? + 1..];
    let mut fields = rest.split_ascii_whitespace();
    let state = fields.next()?.chars().next()?;
    let _ppid = fields.next()?;
    let pgrp = fields.next()?.parse().ok()?;
    Some((state, pgrp))
}

/// True while some process of the group `pgid` exists and is not a zombie (an exited child
/// that nobody has waited for does not keep a mount alive).
pub fn group_alive(pgid: i32) -> bool {
    let Ok(dir) = fs::read_dir("/proc") else {
        return false;
    };
    dir.flatten().any(|entry| {
        let name = entry.file_name();
        let is_pid = name
            .to_str()
            .is_some_and(|n| !n.is_empty() && n.bytes().all(|b| b.is_ascii_digit()));
        if !is_pid {
            return false;
        }
        // A process can vanish between listing and reading: that is simply "not alive".
        match fs::read_to_string(entry.path().join("stat")) {
            Ok(stat) => {
                matches!(parse_stat(&stat), Some((state, pgrp)) if pgrp == pgid && state != 'Z')
            }
            Err(_) => false,
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_plain_and_hostile_command_names() {
        assert_eq!(
            parse_stat("42 (bash) S 1 42 42 0 -1 4194560"),
            Some(('S', 42))
        );
        assert_eq!(
            parse_stat("43 (evil ) Z 1 2 3) Z 9 9 9 9) R 7 55 55 0"),
            Some(('R', 55))
        );
        assert_eq!(parse_stat("44 (a b) Z 1 77 77"), Some(('Z', 77)));
        assert_eq!(parse_stat("garbage"), None);
        assert_eq!(parse_stat("1 (x) S 1"), None);
    }

    #[test]
    fn own_group_is_alive_and_absurd_group_is_not() {
        let own = rustix::process::getpgrp().as_raw_nonzero().get();
        assert!(group_alive(own));
        assert!(!group_alive(i32::MAX));
    }

    #[test]
    fn zombie_group_is_not_alive() {
        use std::os::unix::process::CommandExt;
        let mut child = std::process::Command::new("sh")
            .args(["-c", "exit 0"])
            .process_group(0)
            .spawn()
            .unwrap();
        let pgid = child.id() as i32;
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
        while group_alive(pgid) {
            assert!(
                std::time::Instant::now() < deadline,
                "group {pgid} still alive"
            );
            std::thread::sleep(std::time::Duration::from_millis(10));
        }
        child.wait().unwrap();
    }
}
