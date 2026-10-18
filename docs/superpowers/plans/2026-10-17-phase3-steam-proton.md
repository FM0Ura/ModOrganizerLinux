# Fase 3 — Steam/Proton — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** "Executar" no MO2 inicia jogos e ferramentas Windows via Proton (`umu-run`), com o VFS montado no host, até Skyrim SE + SKSE chegarem ao menu com mods e o SSEEdit gravar na camada overwrite.

**Architecture:** O `mo2-vfs` ganha o pedido `spawn` (inicia o programa num grupo próprio e o acompanha), o evento `exited`, a espera do `wineserver` do prefixo e o comando `--list-mounts`. O MO2 passa a iniciar o helper pelo `distrobox-host-exec` quando está em container (`vfsbridge_linux`), inicia todos os programas pelo helper (`VfsConnector::spawn`) e monta argv/env do Proton com o `ProtonLauncher`, que usa a descoberta da Steam nova do `uibase` (`MOBase::steam`, `MOBase::vdf`). O `game_bethesda` põe "My Games"/AppData dentro do prefixo.

**Tech Stack:** Rust stable (edition 2024; `serde`, `rustix`, `std::process`); C++23/Qt 6 (`QProcess`, `QJsonDocument`, widgets); GoogleTest; bash (e2e); `umu-launcher` 1.4.x e Proton no Bazzite (só nos testes manuais).

**Spec:** `docs/superpowers/specs/2026-10-17-phase3-steam-proton-design.md` (ler inteira antes de cada tarefa; §N são seções dela). Contexto da Fase 2: `docs/superpowers/specs/2026-10-15-phase2-vfs-fuse-design.md`.

## Global Constraints

- Branch `fase-3-steam-proton`. Toda mudança dentro de um subtree (`deps/<nome>`) vai em commit próprio com o assunto prefixado por `deps/<nome>:` (ex.: `deps/modorganizer: Linux: start programs through mo2-vfs`), como diz `deps/README.md`. Mudanças em `mo2-vfs/`, `tests/`, `docs/` e na raiz não levam prefixo de subtree.
- O app (`deps/modorganizer`) continua compilando no Windows **contra o `uibase` upstream**: código compartilhado do app não pode usar API nova do `uibase`. A API nova (`<uibase/linux/vdf.h>`, `<uibase/linux/steam.h>`) só é usada em arquivos `src/linux/*_linux.cpp` do app e em blocos Linux do `game_bethesda`.
- Código Linux do app em `src/linux/<nome>_linux.{h,cpp}`; em arquivo compartilhado, `#ifdef _WIN32`/`#ifndef _WIN32` só para mudanças de 1 a 3 linhas (regras de `deps/modorganizer/src/linux/README.md`). Os métodos novos de `VfsConnector` têm implementação padrão na própria interface; `usvfsconnector.*` não muda.
- O CMake do app não compila Rust. O `mo2-vfs` é encontrado por `MO2_VFS_BINARY` ou ao lado do executável (Fase 2).
- `MO2_VFS_BRIDGE`: `direct` inicia o helper direto; `host` força `distrobox-host-exec`; vazio ou `auto` detecta container (`/run/.containerenv` ou `/.dockerenv`). Todo teste automático do app, o smoke e o e2e definem `MO2_VFS_BRIDGE=direct` (o CI roda num container).
- Em container sem `distrobox-host-exec`, nunca cair de volta para montar dentro do container (§6.1).
- Rust: `cargo fmt --check` e `cargo clippy --all-targets -- -D warnings` limpos; nenhum `unsafe` novo (as APIs de `std::process`, `std::os::unix` e `rustix` bastam).
- Protocolo: `spawn` responde `spawned {pid}` (ou `error`); `exited {pid, code}` é espontâneo; `code` é o código de saída ou `128 + sinal`. O stdout do `mo2-vfs` só tem linhas JSON do protocolo: programas iniciados nunca herdam esse stdout.
- O diretório do servidor do Wine é `<tmp>/.wine-<uid>/server-<dev>-<ino>` (hexadecimal minúsculo, sem zeros à esquerda) do `stat` (seguindo links) de `<wineprefix>/pfx`, ou do próprio `<wineprefix>` se não houver `pfx/`; `<tmp>` é `MO2_VFS_WINE_TMP` se definido (testes), senão `/tmp`.
- Variáveis de lançamento do Proton, nesta ordem, com as do executável por último: `WINEPREFIX=<compatdata>`, `GAMEID=umu-<appid>`, `STORE=steam`, `SteamAppId=<appid>`, `SteamGameId=<appid>`, `PROTONPATH=<pasta do Proton>`, `STEAM_COMPAT_CLIENT_INSTALL_PATH=<raiz da Steam>`. argv: `umu-run <exe> <args>`; `.bat`/`.cmd`: `umu-run cmd /c <arquivo> <args>`.
- Mensagens ao usuário no código-fonte em inglês (traduções na Fase 4). Formatação C++: `clang-format` do subtree.
- Commits terminam com as linhas de atribuição da sessão; mensagens sem identificador de modelo.

## Ambiente de execução

O CI (GitHub Actions, container Fedora 44 com `/dev/fuse` e `SYS_ADMIN`) é a referência de "passou". No Bazzite, compilar e rodar os testes automáticos dentro da distrobox `mo2-run` (`./build.sh --tests`, ou `--only <alvo> --tests`); `cargo` roda em `mo2-vfs/`. Os testes manuais (Task 3 e o checklist da Task 13) rodam o MO2 da distrobox com Steam, Proton e `umu-run` do host.

## Review Focus

1. **Caminhos com espaços e não-ASCII** (`Skyrim Special Edition`, `Mods/Ünïcödé/x.exe`) atravessando JSON, `distrobox-host-exec` e `umu-run`: cada elemento do argv chega intacto → testes `spawn_argv_with_spaces_and_unicode` (Task 1) e `WindowsExeArgv` (Task 9); o e2e usa `Skyrim Special Edition`.
2. **Argumentos com aspas** (`-forcesteamloader "a b" c`): viram três elementos do argv, como no Windows → teste `ArgumentsAreSplitLikeTheCommandLine` (Task 9).
3. **Proton configurado que foi desinstalado** (no executável, ou no `CompatToolMapping` da Steam): erro dizendo qual falta e listando os instalados, nada é montado → testes `ConfiguredProtonMissing` e `SteamMappingProtonMissing` (Task 9).
4. **Variáveis de ambiente mal formadas** no executável (`FOO`, `=x`, `1A=b`, linha vazia): ignoradas com aviso no log; as válidas passam → teste `InvalidEnvironmentEntriesAreDropped` (Task 9).
5. **Duas execuções seguidas no mesmo prefixo** (a segunda enquanto o `wineserver` da primeira ainda encerra): o helper acompanha o prefixo uma vez só e o mount continua até o último → teste `same_prefix_twice_tracked_once` (Task 2).

---

### Task 1: `mo2-vfs` — pedido `spawn`, evento `exited`, sessão própria e stdout fechado

**Files:**
- Create: `mo2-vfs/src/spawn.rs`
- Modify: `mo2-vfs/src/lib.rs` (módulo `spawn`), `mo2-vfs/src/session.rs` (`ErrorCode::SpawnFailed`), `mo2-vfs/src/ipc.rs` (`Request::Spawn`, `Event::Spawned`, `Event::Exited`, filhos no `Server`), `mo2-vfs/src/bin/mo2-vfs.rs` (`setsid()`, `poll_children`, `EPIPE`)
- Test: `mo2-vfs/tests/protocol.rs`

**Interfaces:**
- Produces (protocolo): `{"op":"spawn","argv":[...],"env":{...},"cwd":"...","wineprefix":"..."?,"output":"..."?}` → `{"event":"spawned","pid":N}` ou `{"event":"error","code":"spawn_failed",...}`/`bad_request`; espontâneo `{"event":"exited","pid":N,"code":C}`.
- Produces (Rust): `mo2_vfs::spawn::{start(argv: &[String], env: &BTreeMap<String, String>, cwd: &Path, output: Option<&Path>) -> Result<std::process::Child, SessionError>, exit_code(std::process::ExitStatus) -> i32}`; `Server::poll_children(&mut self) -> Vec<Event>`; `Server::has_watched(&self) -> bool` (substitui `has_groups`).

- [ ] **Step 1: Testes de protocolo (falham)**

Em `tests/protocol.rs`, um auxiliar para eventos espontâneos fora de ordem e os testes:

```rust
/// Events until one matches `pred` (returned), with the ones before it.
fn until(c: &mut Client, what: &str, pred: impl Fn(&Value) -> bool) -> (Value, Vec<Value>) {
    let mut before = Vec::new();
    loop {
        let ev = c
            .try_event(EVENT_TIMEOUT)
            .unwrap_or_else(|| panic!("no {what} within {EVENT_TIMEOUT:?}; got {before:?}"));
        if pred(&ev) {
            return (ev, before);
        }
        before.push(ev);
    }
}

fn spawn_request(argv: &[&str], cwd: &Path) -> Value {
    json!({"op": "spawn", "argv": argv, "env": {}, "cwd": cwd})
}

#[test]
fn spawn_without_mount_runs_and_reports_exit() {
    let env = Env::new();
    let mut c = Client::start(&env);
    let out = env.p("out.txt");
    let script = format!(
        "echo \"$FOO\" > '{0}'; pwd >> '{0}'; cut -d' ' -f5 /proc/$$/stat >> '{0}'; echo $$ >> '{0}'; exit 3",
        out.display()
    );
    let mut req = spawn_request(&["sh", "-c", &script], &env.p("Data"));
    req["env"] = json!({"FOO": "bar"});
    let ev = c.request(req);
    assert_eq!(ev["event"], "spawned", "{ev}");
    let pid = ev["pid"].as_i64().unwrap();
    let (ev, _) = until(&mut c, "exited", |e| e["event"] == "exited");
    assert_eq!(ev, json!({"event": "exited", "pid": pid, "code": 3}));
    let lines: Vec<String> = fs::read_to_string(&out).unwrap().lines().map(String::from).collect();
    assert_eq!(lines[0], "bar");
    assert_eq!(Path::new(&lines[1]), env.p("Data"));
    // its own process group: pgrp == pid
    assert_eq!(lines[2], lines[3]);
    assert_eq!(lines[3], pid.to_string());
}

#[test]
fn spawn_exit_by_signal_is_128_plus_signal() {
    let env = Env::new();
    let mut c = Client::start(&env);
    let ev = c.request(spawn_request(&["sh", "-c", "kill -TERM $$"], &env.root));
    let pid = ev["pid"].as_i64().unwrap();
    let (ev, _) = until(&mut c, "exited", |e| e["event"] == "exited");
    assert_eq!(ev, json!({"event": "exited", "pid": pid, "code": 143}));
}

#[test]
fn spawn_failures() {
    let env = Env::new();
    let mut c = Client::start(&env);
    let ev = c.request(spawn_request(&["/nonexistent/program"], &env.root));
    assert_eq!(ev["event"], "error", "{ev}");
    assert_eq!(ev["code"], "spawn_failed");
    assert_eq!(ev["path"], "/nonexistent/program");
    let missing = env.p("missing-dir");
    let ev = c.request(spawn_request(&["true"], &missing));
    assert_eq!(ev["code"], "spawn_failed", "{ev}");
    assert_eq!(ev["path"], json!(missing));
    let ev = c.request(json!({"op": "spawn", "argv": [], "env": {}, "cwd": env.root}));
    assert_eq!(ev["code"], "bad_request", "{ev}");
    // still serving
    assert_eq!(c.request(json!({"op": "status"}))["event"], "status");
}

#[test]
fn spawn_argv_with_spaces_and_unicode() {
    let env = Env::new();
    let mut c = Client::start(&env);
    let out = env.p("args.txt");
    let script = format!("printf '%s\\n' \"$@\" > '{}'", out.display());
    let ev = c.request(spawn_request(
        &["sh", "-c", &script, "sh", "Skyrim Special Edition", "Ünïcödé/x.exe", "a \"b\" c"],
        &env.root,
    ));
    assert_eq!(ev["event"], "spawned", "{ev}");
    until(&mut c, "exited", |e| e["event"] == "exited");
    assert_eq!(
        fs::read_to_string(&out).unwrap(),
        "Skyrim Special Edition\nÜnïcödé/x.exe\na \"b\" c\n"
    );
}

#[test]
fn spawn_output_goes_to_the_file_not_to_the_protocol() {
    let env = Env::new();
    let mut c = Client::start(&env);
    let log = env.p("run.log");
    let mut req = spawn_request(&["sh", "-c", "echo not-json; echo on-stderr >&2"], &env.root);
    req["output"] = json!(log);
    c.request(req);
    // a non-JSON line on stdout would make the client panic
    until(&mut c, "exited", |e| e["event"] == "exited");
    assert_eq!(fs::read_to_string(&log).unwrap(), "not-json\non-stderr\n");
    // without output, nothing reaches the protocol either
    c.request(spawn_request(&["sh", "-c", "echo not-json"], &env.root));
    until(&mut c, "exited", |e| e["event"] == "exited");
}

#[test]
fn spawn_while_mounted_is_watched_and_exited_comes_before_unmounted() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let mut c = Client::start(&env);
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    let ev = c.request(spawn_request(&["sh", "-c", "cat a.txt > ../seen.txt; sleep 0.5"], &env.p("Data")));
    let pid = ev["pid"].as_i64().unwrap();
    let st = c.request(json!({"op": "status"}));
    assert_eq!(st["groups"], json!([pid]));
    let (ev, before) = until(&mut c, "unmounted", |e| e["event"] == "unmounted");
    assert_eq!(ev, json!({"event": "unmounted"}));
    assert_eq!(before, vec![json!({"event": "exited", "pid": pid, "code": 0})]);
    assert_eq!(fs::read_to_string(env.p("seen.txt")).unwrap(), "a");
    assert!(!is_mounted(&env.p("Data")));
}

#[test]
fn mo2_vfs_is_session_leader() {
    let env = Env::new();
    let c = Client::start(&env);
    let pid = c.child.id();
    let stat = fs::read_to_string(format!("/proc/{pid}/stat")).unwrap();
    let rest: Vec<&str> = stat[stat.rfind(')').unwrap() + 2..].split(' ').collect();
    // state ppid pgrp session
    assert_eq!(rest[3], pid.to_string());
}

#[test]
fn closed_stdout_is_not_a_warning() {
    let env = Env::new();
    let mut child = env
        .command()
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    let mut stdin = child.stdin.take().unwrap();
    drop(child.stdout.take()); // the application is gone
    writeln!(stdin, "{}", json!({"op": "status"})).unwrap();
    drop(stdin);
    let out = child.wait_with_output().unwrap();
    assert!(out.status.success());
    let stderr = String::from_utf8_lossy(&out.stderr);
    assert!(!stderr.contains("cannot write to stdout"), "{stderr}");
}
```

O `Client::start_with` já sobe o processo com `Stdio::null()` no stderr; o teste `closed_stdout_is_not_a_warning` usa `env.command()` direto, sem `--log`, então os logs vão para o stderr capturado.

- [ ] **Step 2: Rodar e ver falhar**

Run: `cargo test --test protocol spawn_ mo2_vfs_is_session_leader closed_stdout`
Expected: FAIL (`bad_request` "invalid request: unknown variant `spawn`"; `session` diferente do pid; aviso no stderr).

- [ ] **Step 3: `ErrorCode::SpawnFailed` e `spawn.rs`**

Em `session.rs`, acrescentar a variante (o `serde` existente já usa `snake_case`, vira `"spawn_failed"`):

```rust
    /// A program of a `spawn` request could not be started.
    SpawnFailed,
```

`src/spawn.rs`:

```rust
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
```

Em `lib.rs`, `pub mod spawn;` com uma linha na doc do crate (como os outros módulos).

- [ ] **Step 4: `Server` com filhos**

Em `ipc.rs`:

```rust
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
```

em `Request`, e em `Event`:

```rust
    Spawned {
        pid: i32,
    },
    Exited {
        pid: i32,
        code: i32,
    },
```

`Server` ganha `children: Vec<std::process::Child>` (o `#[derive(Default)]` continua valendo). No `handle`:

```rust
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
```

(`wineprefix` é usado na Task 2.) Novos métodos:

```rust
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

    /// True while something keeps the mount alive (watched process groups).
    pub fn has_watched(&self) -> bool {
        !self.groups.is_empty()
    }
```

`has_groups` é renomeado para `has_watched` (e os usos no binário trocados). Os testes unitários de `ipc.rs` que montam `Request` por JSON continuam valendo; acrescentar ao teste de `parse_request` o caso `{"op":"spawn","argv":["x"],"cwd":"/"}` → `Request::Spawn` com `env` vazio e `wineprefix`/`output` `None`.

- [ ] **Step 5: Binário — `setsid`, filhos, `EPIPE`**

Em `src/bin/mo2-vfs.rs`:

```rust
fn emit(event: &Event) {
    let mut out = std::io::stdout().lock();
    let line = serde_json::to_string(event).expect("events serialize");
    match writeln!(out, "{line}").and_then(|()| out.flush()) {
        Ok(()) => {}
        // the application closed its end (it exited or crashed): nothing to report
        Err(e) if e.kind() == std::io::ErrorKind::BrokenPipe => {
            tracing::debug!("stdout is closed, event dropped: {line}")
        }
        Err(e) => tracing::warn!(error = %e, "cannot write to stdout"),
    }
}
```

No início de `serve()`, antes dos sinais:

```rust
    // a session of its own whoever started it (QProcess with setsid() in the child, or
    // host-spawn through distrobox-host-exec): the signals of a terminal don't reach it;
    // fails harmlessly when it already leads a session or a process group
    if let Err(e) = rustix::process::setsid() {
        tracing::debug!(error = %e, "setsid");
    }
```

Se `setsid()` falhar com `EPERM` por ser líder de grupo (caso do `host-spawn`), o processo continua na sessão em que já está, que é dele próprio nesse caso (`pid == pgid == sid`, visto no spike). Se o teste `mo2_vfs_is_session_leader` falhar porque o `Command` do teste não cria sessão nem grupo, o `setsid()` tem de dar certo ali (o processo do teste não é líder de grupo); não contornar o teste.

No laço, logo depois do `match rx.recv_timeout(POLL)` e antes do `poll_groups`:

```rust
        for event in server.poll_children() {
            emit(&event);
        }
```

e `server.has_groups()` → `server.has_watched()` nos dois lugares.

- [ ] **Step 6: Rodar tudo**

Run: `cargo fmt --check && cargo clippy --all-targets -- -D warnings && cargo test`
Expected: PASS (os testes com FUSE pulam sem `/dev/fuse`, exceto com `MO2_REQUIRE_FUSE=1`).

- [ ] **Step 7: Commit**

```bash
git add mo2-vfs/src mo2-vfs/tests/protocol.rs
git commit -m "mo2-vfs: spawn request, exited events and a session of its own"
```

---

### Task 2: `mo2-vfs` — espera do `wineserver`, `status.processes` e `--list-mounts`

**Files:**
- Modify: `mo2-vfs/src/spawn.rs` (`wineserver_dir`, `wineservers`), `mo2-vfs/src/ipc.rs` (`prefixes`, `poll_groups`, `status`), `mo2-vfs/src/bin/mo2-vfs.rs` (`--list-mounts`)
- Test: `mo2-vfs/src/spawn.rs` (unitários), `mo2-vfs/tests/protocol.rs`

**Interfaces:**
- Consumes: Task 1 (`Request::Spawn { wineprefix, .. }`, `Server::children`, `has_watched`).
- Produces: `mo2_vfs::spawn::{wineserver_dir(wineprefix: &Path) -> std::io::Result<PathBuf>, wineservers(server_dir: &Path) -> Vec<i32>}`; `Event::Status` com `processes: Vec<i32>`; `mo2-vfs --list-mounts` → uma linha `{"roots":[...]}`, código 0.

- [ ] **Step 1: Testes unitários (falham)**

No fim de `spawn.rs`:

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::fs::MetadataExt;

    fn expected(dir: &Path, tmp: &Path) -> PathBuf {
        let md = std::fs::metadata(dir).unwrap();
        tmp.join(format!(".wine-{}", rustix::process::getuid().as_raw()))
            .join(format!("server-{:x}-{:x}", md.dev(), md.ino()))
    }

    #[test]
    fn server_dir_of_a_proton_prefix_is_the_one_of_pfx() {
        let t = tempfile::tempdir().unwrap();
        let compat = t.path().join("compatdata/489830");
        std::fs::create_dir_all(compat.join("pfx")).unwrap();
        let got = wineserver_dir_in(&compat, t.path()).unwrap();
        assert_eq!(got, expected(&compat.join("pfx"), t.path()));
    }

    #[test]
    fn server_dir_of_a_plain_prefix_and_of_a_link() {
        let t = tempfile::tempdir().unwrap();
        let plain = t.path().join("wine");
        std::fs::create_dir_all(&plain).unwrap();
        assert_eq!(wineserver_dir_in(&plain, t.path()).unwrap(), expected(&plain, t.path()));
        // pfx as a link: stat follows it, like Wine
        let compat = t.path().join("compat");
        std::fs::create_dir_all(&compat).unwrap();
        std::os::unix::fs::symlink(&plain, compat.join("pfx")).unwrap();
        assert_eq!(wineserver_dir_in(&compat, t.path()).unwrap(), expected(&plain, t.path()));
        assert!(wineserver_dir_in(&t.path().join("missing"), t.path()).is_err());
    }

    #[test]
    fn wineservers_matches_name_and_working_directory() {
        let t = tempfile::tempdir().unwrap();
        let server = t.path().join("server-1-2");
        std::fs::create_dir_all(&server).unwrap();
        let fake = t.path().join("wineserver");
        std::fs::write(&fake, "#!/bin/sh\nsleep 5\n").unwrap();
        std::fs::set_permissions(&fake, std::os::unix::fs::PermissionsExt::from_mode(0o755))
            .unwrap();
        let mut child = Command::new(&fake).current_dir(&server).spawn().unwrap();
        let pid = child.id() as i32;
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
        while !wineservers(&server).contains(&pid) {
            assert!(std::time::Instant::now() < deadline, "fake wineserver not found");
            std::thread::sleep(std::time::Duration::from_millis(20));
        }
        assert!(wineservers(&t.path().join("other")).is_empty());
        child.kill().unwrap();
        child.wait().unwrap();
        assert!(!wineservers(&server).contains(&pid));
    }
}
```

(Um script `#!/bin/sh` executado direto tem `comm` igual ao nome do arquivo, `wineserver`; ele espera o `sleep`, sem `exec`, para manter o nome.)

- [ ] **Step 2: Testes de protocolo (falham)**

Em `tests/protocol.rs`:

```rust
/// A compatdata directory with pfx/, its Wine server directory under `tmp`, and a fake
/// wineserver script that lives `secs` seconds in it, started detached by `launcher`.
struct FakePrefix {
    compat: PathBuf,
    server: PathBuf,
    launcher: PathBuf,
}

impl FakePrefix {
    fn new(env: &Env, secs: f32) -> FakePrefix {
        use std::os::unix::fs::{MetadataExt, PermissionsExt};
        let compat = env.p("compatdata/489830");
        fs::create_dir_all(compat.join("pfx")).unwrap();
        let md = fs::metadata(compat.join("pfx")).unwrap();
        let server = env
            .p("winetmp")
            .join(format!(".wine-{}", rustix::process::getuid().as_raw()))
            .join(format!("server-{:x}-{:x}", md.dev(), md.ino()));
        fs::create_dir_all(&server).unwrap();
        let wineserver = env.p("bin/wineserver");
        env.write("bin/wineserver", &format!("#!/bin/sh\nsleep {secs}\n"));
        fs::set_permissions(&wineserver, fs::Permissions::from_mode(0o755)).unwrap();
        // like skse64_loader.exe: starts something outside its group and exits at once
        let launcher = env.p("bin/launcher");
        env.write(
            "bin/launcher",
            &format!(
                "#!/bin/sh\ncd '{}' && setsid '{}' &\nexit 0\n",
                server.display(),
                wineserver.display()
            ),
        );
        fs::set_permissions(&launcher, fs::Permissions::from_mode(0o755)).unwrap();
        FakePrefix { compat, server, launcher }
    }
}

fn wine_env(env: &Env) -> Command {
    let mut c = env.command();
    c.env("MO2_VFS_WINE_TMP", env.p("winetmp"));
    c
}

#[test]
fn mount_kept_while_the_prefix_wineserver_lives() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let fake = FakePrefix::new(&env, 2.0);
    let mut c = Client::start_with(wine_env(&env));
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    let mut req = spawn_request(&[fake.launcher.to_str().unwrap()], &env.root);
    req["wineprefix"] = json!(fake.compat);
    let pid = c.request(req)["pid"].as_i64().unwrap();
    let (ev, _) = until(&mut c, "exited", |e| e["event"] == "exited");
    assert_eq!(ev["pid"], pid);
    let started = Instant::now();
    // the launcher's group ended, the wineserver still runs: still mounted
    thread::sleep(Duration::from_millis(800));
    assert!(is_mounted(&env.p("Data")));
    let st = c.request(json!({"op": "status"}));
    assert_eq!(st["processes"].as_array().unwrap().len(), 1, "{st}");
    let (ev, _) = until(&mut c, "unmounted", |e| e["event"] == "unmounted");
    assert_eq!(ev, json!({"event": "unmounted"}));
    let waited = started.elapsed();
    assert!(waited > Duration::from_millis(900) && waited < Duration::from_secs(4), "{waited:?}");
    let _ = fake.server;
}

#[test]
fn without_wineprefix_the_group_alone_holds_the_mount() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let fake = FakePrefix::new(&env, 3.0);
    let mut c = Client::start_with(wine_env(&env));
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    c.request(spawn_request(&[fake.launcher.to_str().unwrap()], &env.root));
    let started = Instant::now();
    until(&mut c, "unmounted", |e| e["event"] == "unmounted");
    assert!(started.elapsed() < Duration::from_secs(2));
}

#[test]
fn same_prefix_twice_tracked_once() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let fake = FakePrefix::new(&env, 2.0);
    let mut c = Client::start_with(wine_env(&env));
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    for _ in 0..2 {
        let mut req = spawn_request(&[fake.launcher.to_str().unwrap()], &env.root);
        req["wineprefix"] = json!(fake.compat);
        assert_eq!(c.request(req)["event"], "spawned");
    }
    until(&mut c, "exited", |e| e["event"] == "exited");
    until(&mut c, "exited", |e| e["event"] == "exited");
    thread::sleep(Duration::from_millis(600));
    let st = c.request(json!({"op": "status"}));
    // two fake wineservers in the same directory, the prefix is listed once: both pids
    // are processes, and the mount ends after the last
    assert_eq!(st["processes"].as_array().unwrap().len(), 2, "{st}");
    until(&mut c, "unmounted", |e| e["event"] == "unmounted");
}

#[test]
fn status_lists_running_programs() {
    let env = Env::new();
    let mut c = Client::start(&env);
    let pid = c.request(spawn_request(&["sleep", "1"], &env.root))["pid"].as_i64().unwrap();
    let st = c.request(json!({"op": "status"}));
    assert_eq!(st["processes"], json!([pid]));
    until(&mut c, "exited", |e| e["event"] == "exited");
    assert_eq!(c.request(json!({"op": "status"}))["processes"], json!([]));
}

#[test]
fn list_mounts() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let list = || -> Value {
        let out = env.command().arg("--list-mounts").output().unwrap();
        assert!(out.status.success());
        serde_json::from_slice(&out.stdout).unwrap()
    };
    assert!(!list()["roots"].as_array().unwrap().contains(&json!(env.p("Data"))));
    let mut c = Client::start(&env);
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    assert!(list()["roots"].as_array().unwrap().contains(&json!(env.p("Data"))));
    c.request(json!({"op": "unmount"}));
}
```

Atualizar os testes existentes que comparam o evento `status` inteiro para incluir `"processes": []`.

- [ ] **Step 3: Rodar e ver falhar**

Run: `cargo test spawn:: && cargo test --test protocol wineserver same_prefix status_lists list_mounts without_wineprefix`
Expected: FAIL (funções inexistentes; desmonta com o grupo; `processes` ausente; `--list-mounts` desconhecido).

- [ ] **Step 4: Implementar em `spawn.rs`**

```rust
use std::fs;
use std::os::unix::fs::MetadataExt;
use std::path::PathBuf;

/// The directory of the Wine server of a prefix (§5.3 of the Phase 3 spec), under `tmp`.
pub(crate) fn wineserver_dir_in(wineprefix: &Path, tmp: &Path) -> std::io::Result<PathBuf> {
    let pfx = wineprefix.join("pfx");
    let dir = if pfx.is_dir() { pfx } else { wineprefix.to_path_buf() };
    let md = fs::metadata(&dir)?;
    Ok(tmp
        .join(format!(".wine-{}", rustix::process::getuid().as_raw()))
        .join(format!("server-{:x}-{:x}", md.dev(), md.ino())))
}

/// `<tmp>/.wine-<uid>/server-<dev>-<ino>` of `<wineprefix>/pfx` (or of `wineprefix`
/// without pfx/), where the wineserver of that prefix runs; `<tmp>` is
/// `MO2_VFS_WINE_TMP` (tests) or `/tmp`, as Wine uses.
pub fn wineserver_dir(wineprefix: &Path) -> std::io::Result<PathBuf> {
    let tmp = std::env::var_os("MO2_VFS_WINE_TMP")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/tmp"));
    wineserver_dir_in(wineprefix, &tmp)
}

/// The pids of the live processes named `wineserver` whose working directory is
/// `server_dir` (readable for processes of the same user, also inside pressure-vessel).
pub fn wineservers(server_dir: &Path) -> Vec<i32> {
    let Ok(dir) = fs::read_dir("/proc") else {
        return Vec::new();
    };
    dir.flatten()
        .filter_map(|entry| {
            let pid: i32 = entry.file_name().to_str()?.parse().ok()?;
            let comm = fs::read_to_string(entry.path().join("comm")).ok()?;
            if comm.trim_end() != "wineserver" {
                return None;
            }
            let stat = fs::read_to_string(entry.path().join("stat")).ok()?;
            let state = stat[stat.rfind(')')? + 1..].split_ascii_whitespace().next()?;
            if state == "Z" {
                return None;
            }
            (fs::read_link(entry.path().join("cwd")).ok()? == server_dir).then_some(pid)
        })
        .collect()
}
```

- [ ] **Step 5: Implementar no `Server`**

Campo `prefixes: Vec<PathBuf>` (diretórios de servidor). No `Spawn`, depois de acompanhar o grupo, só com mount:

```rust
                        if let (Some(_), Some(prefix)) = (&self.session, &wineprefix) {
                            match crate::spawn::wineserver_dir(prefix) {
                                Ok(dir) if !self.prefixes.contains(&dir) => self.prefixes.push(dir),
                                Ok(_) => {}
                                Err(e) => tracing::warn!(
                                    prefix = %prefix.display(), error = %e,
                                    "cannot find the Wine server directory of the prefix"
                                ),
                            }
                        }
```

`poll_groups` passa a começar assim (o resto igual):

```rust
        if self.groups.is_empty() && self.prefixes.is_empty() {
            return None;
        }
        self.groups.retain(|&g| group_alive(g));
        if !self.groups.is_empty() {
            return None;
        }
        // programs that left the watched groups (skse64_loader.exe starting the game) keep
        // the wineserver of their prefix alive until the last Wine process ends
        self.prefixes
            .retain(|dir| !crate::spawn::wineservers(dir).is_empty());
        if !self.prefixes.is_empty() {
            return None;
        }
```

`unmount()` também limpa `self.prefixes`. `has_watched()` vira `!self.groups.is_empty() || !self.prefixes.is_empty()`. `Event::Status` ganha `processes: Vec<i32>`, preenchido com os pids de `self.children` mais os de `wineservers(dir)` de cada `self.prefixes` (sem repetir).

- [ ] **Step 6: `--list-mounts`**

Em `Args`, `list_mounts: bool` (`"--list-mounts" => args.list_mounts = true`), texto de uso `| --list-mounts`, e:

```rust
fn list_mounts() -> ExitCode {
    let line = json!({"roots": state::mounted_mo2_vfs()});
    // the reader may be gone: not an error worth a panic
    let _ = writeln!(std::io::stdout().lock(), "{line}");
    ExitCode::SUCCESS
}
```

com `if args.cleanup { cleanup() } else if args.list_mounts { list_mounts() } else { serve() }`. Trocar também os `println!` de `cleanup()` por `writeln!` com o resultado ignorado (pendência da Fase 4 "`println!` do modo de limpeza entra em pânico com EPIPE"; risca-la no `STATE.md` na Task 13).

- [ ] **Step 7: Rodar tudo**

Run: `cargo fmt --check && cargo clippy --all-targets -- -D warnings && cargo test`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add mo2-vfs/src mo2-vfs/tests/protocol.rs
git commit -m "mo2-vfs: keep the mount while the prefix wineserver lives; --list-mounts"
```

---

### Task 3: Ponto de controle — SKSE real no Bazzite (manual, com o usuário)

Antes de integrar o MO2, confirmar no Proton real o risco principal do §11: `skse64_loader.exe` inicia o `SkyrimSE.exe` e sai, e a espera do `wineserver` segura o mount até o jogo fechar. Código descartável, fora do repositório.

**Files:**
- Create (descartável, no scratchpad da sessão, nunca no repositório): `skse_checkpoint.py`

**Interfaces:**
- Consumes: binário `mo2-vfs` das Tasks 1 e 2 (`./build.sh --only mo2-vfs` → `install/bin/mo2-vfs`).

- [ ] **Step 1: Pedir ao usuário que instale o SKSE**

O usuário instala o SKSE64 compatível com a versão do jogo na pasta do jogo (`skse64_loader.exe`, `skse64_*.dll` ao lado do `SkyrimSE.exe`), como mandam as instruções do SKSE. Não instalar nada pelo usuário.

- [ ] **Step 2: Script do ponto de controle**

No host (fora da distrobox), em `<scratchpad>/skse_checkpoint.py`:

```python
#!/usr/bin/env python3
# THROWAWAY Phase 3 checkpoint: mounts a marker mod over Skyrim SE Data with mo2-vfs,
# spawns skse64_loader.exe through umu-run with the "spawn" request (wineprefix set),
# and logs the events and whether SkyrimSE.exe still runs when "unmounted" arrives.
import json, os, subprocess, sys, time
VFS, OUT = sys.argv[1], sys.argv[2]
HOME = os.path.expanduser("~")
STEAM = HOME + "/.local/share/Steam"
GAME = STEAM + "/steamapps/common/Skyrim Special Edition"
COMPAT = STEAM + "/steamapps/compatdata/489830"
S = os.path.dirname(os.path.abspath(__file__))
MOD, OVW = S + "/ckpt/mods/marker", S + "/ckpt/overwrite"
os.makedirs(MOD, exist_ok=True); os.makedirs(OVW, exist_ok=True)
open(MOD + "/MO2_CHECKPOINT_MARKER.txt", "w").write("marker\n")
log = open(OUT, "w", buffering=1)
def L(*a):
    s = f"{time.monotonic():10.2f} " + " ".join(map(str, a)); print(s); log.write(s + "\n")
def skyrim_running():
    for p in os.listdir("/proc"):
        try:
            if p.isdigit() and open(f"/proc/{p}/comm").read().strip() == "SkyrimSE.exe":
                return True
        except OSError:
            pass
    return False
vfs = subprocess.Popen([VFS, "--log", OUT + ".vfs.log", "--log-level", "debug"],
                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
def send(o): vfs.stdin.write(json.dumps(o) + "\n"); vfs.stdin.flush()
def recv():
    line = vfs.stdout.readline(); L("<", line.strip()); return json.loads(line)
recv()
send({"op": "mount", "mappings": [
    {"source": MOD, "destination": GAME + "/Data", "is_directory": True, "create_target": False},
    {"source": OVW, "destination": GAME + "/Data", "is_directory": True, "create_target": True}]})
assert recv()["event"] == "ready"
proton = STEAM + "/steamapps/common/Proton - Experimental"
send({"op": "spawn", "argv": ["umu-run", GAME + "/skse64_loader.exe"], "cwd": GAME,
      "wineprefix": COMPAT, "output": OUT + ".game.log",
      "env": {"WINEPREFIX": COMPAT, "GAMEID": "umu-489830", "STORE": "steam",
              "SteamAppId": "489830", "SteamGameId": "489830", "PROTONPATH": proton,
              "STEAM_COMPAT_CLIENT_INSTALL_PATH": STEAM}})
L("spawn reply", recv())
seen_game = False
while True:
    ev = recv()
    seen_game = seen_game or skyrim_running()
    if ev["event"] == "exited":
        L("exited; SkyrimSE.exe running now:", skyrim_running())
    if ev["event"] == "unmounted":
        L("unmounted; SkyrimSE.exe running now:", skyrim_running(), "| seen during run:", seen_game)
        break
vfs.stdin.close(); vfs.wait(10)
link = COMPAT + "/pfx/drive_c/users/" + os.environ.get("USER", "")
if os.path.islink(link):
    os.unlink(link)  # umu's users/<user> -> steamuser link (spec §1.1), removed here
```

O laço só lê eventos, e o `recv()` bloqueia; por isso `seen_game` só é atualizado quando chega um evento. Isso basta, porque o `exited` do `umu-run` e o `unmounted` são os eventos de interesse.

- [ ] **Step 3: Rodar com o usuário**

Run (host): `python3 -I <scratchpad>/skse_checkpoint.py install/bin/mo2-vfs <scratchpad>/ckpt.log` (em segundo plano). O usuário espera o menu principal, abre o console (`~`), digita `getskseversion` (deve responder a versão do SKSE) e sai pelo menu.

- [ ] **Step 4: Decidir**

Passa quando: `unmounted` chega com `SkyrimSE.exe running now: False` e `seen during run: True`, e o console respondeu ao `getskseversion`. Depois confirmar `findmnt | grep -i skyrim` vazio no host e nenhum `MO2_CHECKPOINT_*` no `Data` real.

Se falhar (desmonta com o jogo aberto): parar o plano e voltar ao design com o log. A alternativa já prevista no §11 é rodar `<proton>/files/bin/wineserver -w` com o `WINEPREFIX` no host como sinal de fim. Não seguir para a Task 4 sem passar. Registrar o resultado (passou ou não, tempos de `exited` e `unmounted`) no relatório da tarefa, para a Task 13 levar ao §1.2 da spec.

---

### Task 4: `uibase` — parser de VDF público, com `libraryfolders.vdf` refeito sobre ele

**Files:**
- Create: `deps/uibase/include/uibase/linux/vdf.h`, `deps/uibase/src/linux/vdf_linux.cpp`, `deps/uibase/tests/test_linux_vdf.cpp`
- Modify: `deps/uibase/src/linux/steamutility_linux.cpp` (usa o parser novo; tokenizador sai daqui), `deps/uibase/src/CMakeLists.txt` (header em `linux_headers`, fonte na lista Linux), `deps/uibase/tests/CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
namespace MOBase::vdf
{
// one entry of a text VDF file (Steam's KeyValues): a key with a string value, or a
// block of entries
struct QDLLEXPORT Node
{
  QString key;
  QString value;
  std::vector<Node> children;
  bool block = false;

  // the last child with this key, ignoring case (Steam keeps the last of repeated
  // keys); null if there is none
  const Node* child(QStringView key) const;

  // the value of child(key), empty if there is none or it is a block
  QString value(QStringView key) const;

  // child(keys[0])->child(keys[1])...; null as soon as one is missing
  const Node* path(std::initializer_list<QStringView> keys) const;
};

// the entries of a text VDF document, as the children of a block node with an empty
// key; parsing stops at the first malformed token (unterminated string, unexpected
// brace) and keeps what came before it; nesting deeper than 64 levels stops it too.
QDLLEXPORT Node parse(const QByteArray& data);

// parse() of a file of at most 16 MiB; nullopt if it can't be read or is bigger
QDLLEXPORT std::optional<Node> parseFile(const QString& path);
}  // namespace MOBase::vdf
```

- [ ] **Step 1: Testes (falham)**

`tests/test_linux_vdf.cpp` (GoogleTest, como os outros `test_linux_*.cpp`):

```cpp
#include <uibase/linux/vdf.h>

#include <gtest/gtest.h>

using namespace MOBase;

TEST(LinuxVdf, NestedBlocksAndValues)
{
  const auto root = vdf::parse(R"("AppState"
{
	"appid"		"489830"
	"installdir"		"Skyrim Special Edition"
	"UserConfig" { "language" "english" }
})");
  ASSERT_TRUE(root.block);
  EXPECT_EQ(root.path({u"appstate"})->value(u"InstallDir"), "Skyrim Special Edition");
  EXPECT_EQ(root.path({u"AppState", u"userconfig"})->value(u"language"), "english");
  EXPECT_EQ(root.path({u"AppState", u"missing"}), nullptr);
  EXPECT_EQ(root.child(u"AppState")->value(u"UserConfig"), "");
}

TEST(LinuxVdf, EscapesCommentsConditionalsAndBareWords)
{
  const auto root = vdf::parse("// comment\n\"a\" \"C:\\\\Games\\\\\\\"x\\\"\"\n"
                               "b bare [$WIN32]\n\"c\" \"tab\\tline\\n\"\n");
  EXPECT_EQ(root.value(u"a"), "C:\\Games\\\"x\"");
  EXPECT_EQ(root.value(u"b"), "bare");
  EXPECT_EQ(root.value(u"c"), "tab\tline\n");
}

TEST(LinuxVdf, RepeatedKeyLastWins)
{
  const auto root = vdf::parse(R"("k" "1" "K" "2")");
  EXPECT_EQ(root.value(u"k"), "2");
  EXPECT_EQ(root.children.size(), 2u);
}

TEST(LinuxVdf, MalformedKeepsWhatCameBefore)
{
  const auto root = vdf::parse("\"a\" \"1\" \"b\" { \"c\" \"2\" } \"d\" \"unterminated");
  EXPECT_EQ(root.value(u"a"), "1");
  EXPECT_EQ(root.path({u"b"})->value(u"c"), "2");
  EXPECT_EQ(root.child(u"d"), nullptr);

  EXPECT_TRUE(vdf::parse("}").children.empty());
  EXPECT_TRUE(vdf::parse("").children.empty());
}

TEST(LinuxVdf, DepthIsBounded)
{
  QByteArray deep;
  for (int i = 0; i < 100; ++i) {
    deep += "\"k\" { ";
  }
  const auto root = vdf::parse(deep);
  int depth        = 0;
  for (const auto* n = &root; n && !n->children.empty(); n = &n->children.front()) {
    ++depth;
  }
  EXPECT_LE(depth, 65);
}

TEST(LinuxVdf, ParseFile)
{
  EXPECT_FALSE(vdf::parseFile("/nonexistent/file.vdf"));
}
```

Os testes existentes de `findSteamGame` (`test_linux_utility.cpp`), com os dois formatos de `libraryfolders.vdf` e o malformado, continuam valendo sem mudança.

- [ ] **Step 2: Rodar e ver falhar**

Run (na distrobox): `./build.sh --only uibase --tests`
Expected: FAIL de compilação (`uibase/linux/vdf.h` não existe).

- [ ] **Step 3: Implementar**

`src/linux/vdf_linux.cpp`: mover para cá o `VdfTokenizer` de `steamutility_linux.cpp`, sem mudança, e montar a árvore:

```cpp
// Text VDF (Steam KeyValues) reader of include/uibase/linux/vdf.h. The files are
// untrusted input: size and nesting depth are bounded and malformed content ends the
// parse, keeping what came before it.

#include "linux/vdf.h"

#include <QFile>

namespace MOBase::vdf
{
namespace
{
  constexpr qint64 MaxVdfSize    = 16 * 1024 * 1024;
  constexpr std::size_t MaxDepth = 64;

  // class VdfTokenizer { ... }  (moved unchanged from steamutility_linux.cpp)

  // entries until the closing brace of this block (or the end); false when the parse
  // must stop (malformed, too deep)
  bool parseBlock(VdfTokenizer& tokens, Node& block, std::size_t depth, bool inner)
  {
    for (;;) {
      const auto key = tokens.next();

      if (key.kind == VdfTokenizer::Kind::Close) {
        return inner;  // a closing brace at the top level is malformed
      }
      if (key.kind != VdfTokenizer::Kind::String) {
        return key.kind == VdfTokenizer::Kind::End && !inner;
      }

      const auto value = tokens.next();
      Node node;
      node.key = QString::fromUtf8(key.text);

      if (value.kind == VdfTokenizer::Kind::Open) {
        if (depth + 1 > MaxDepth) {
          return false;
        }
        node.block = true;
        const bool ok = parseBlock(tokens, node, depth + 1, true);
        block.children.push_back(std::move(node));
        if (!ok) {
          return false;
        }
        continue;
      }

      if (value.kind != VdfTokenizer::Kind::String) {
        return false;
      }

      node.value = QString::fromUtf8(value.text);
      block.children.push_back(std::move(node));
    }
  }
}  // namespace

const Node* Node::child(QStringView k) const
{
  const Node* found = nullptr;
  for (const auto& c : children) {
    if (QStringView(c.key).compare(k, Qt::CaseInsensitive) == 0) {
      found = &c;
    }
  }
  return found;
}

QString Node::value(QStringView k) const
{
  const auto* c = child(k);
  return (c && !c->block) ? c->value : QString();
}

const Node* Node::path(std::initializer_list<QStringView> keys) const
{
  const Node* n = this;
  for (auto k : keys) {
    n = n->child(k);
    if (!n) {
      return nullptr;
    }
  }
  return n;
}

Node parse(const QByteArray& data)
{
  Node root;
  root.block = true;
  VdfTokenizer tokens(data);
  parseBlock(tokens, root, 0, false);
  return root;
}

std::optional<Node> parseFile(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > MaxVdfSize) {
    return {};
  }
  return parse(file.read(MaxVdfSize));
}
}  // namespace MOBase::vdf
```

Um bloco incompleto (`"b" { "c" "2"` sem fechar) fica na árvore com o que tinha; o teste `MalformedKeepsWhatCameBefore` cobre isso.

`steamutility_linux.cpp`: `parseLibraryFolders()` passa a usar o parser. Os caminhos são os valores `path` dos blocos dentro do bloco raiz (formato atual) ou os valores com chave numérica direto no bloco raiz (formato antigo), qualquer que seja a chave raiz (`libraryfolders`, `LibraryFolders`):

```cpp
  QStringList parseLibraryFolders(const QByteArray& data)
  {
    QStringList paths;
    const auto root = vdf::parse(data);

    for (const auto& top : root.children) {
      for (const auto& entry : top.children) {
        const QString path = entry.block ? entry.value(u"path")
                             : isNumber(entry.key.toUtf8()) ? entry.value
                                                            : QString();
        if (!path.isEmpty()) {
          paths.append(path);
        }
      }
    }

    return paths;
  }
```

O comentário do topo do arquivo passa a citar `vdf_linux.cpp` e `steam_linux.cpp` (Task 5) em vez de "a complete Steam module comes later". Os limites `MaxVdfSize`/`MaxDepth` saem deste arquivo; `libraryFolders()` lê com `vdf::parseFile()`.

CMake: `../include/uibase/linux/vdf.h` em `linux_headers`, `linux/vdf_linux.cpp` na lista de fontes Linux (junto de `linux/steamutility_linux.cpp`), `test_linux_vdf.cpp` na lista do `target_sources(uibase-tests ...)` Linux.

- [ ] **Step 4: Rodar**

Run: `./build.sh --only uibase --tests`
Expected: PASS (inclusive os testes de `findSteamGame` existentes).

- [ ] **Step 5: Commit**

```bash
git add deps/uibase
git commit -m "deps/uibase: Linux: public text VDF parser, used for libraryfolders.vdf"
```

---

### Task 5: `uibase` — descoberta da Steam (raízes, jogos, prefixos, Protons)

**Files:**
- Create: `deps/uibase/include/uibase/linux/steam.h`, `deps/uibase/src/linux/steam_linux.cpp`, `deps/uibase/tests/test_linux_steam.cpp`
- Modify: `deps/uibase/src/linux/steamutility_linux.cpp` (`findSteam()`/`findSteamGame()` sobre `steam::roots()`/`steam::libraries()`), `deps/uibase/src/CMakeLists.txt`, `deps/uibase/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 4 (`vdf::parse`, `vdf::parseFile`, `Node::path/value/child`).
- Produces:

```cpp
namespace MOBase::steam
{
struct QDLLEXPORT App
{
  QString appId;
  QString root;        // Steam installation (STEAM_COMPAT_CLIENT_INSTALL_PATH)
  QString library;     // library with the game
  QString gamePath;    // <library>/steamapps/common/<installdir>
  QString compatData;  // compatdata/<appid> with pfx/, or empty (never run with Proton)
};

struct QDLLEXPORT Proton
{
  QString displayName;   // "Proton - Experimental", "GE-Proton11-7"
  QString internalName;  // name used by config.vdf: "proton_experimental", "GE-Proton11-7"
  QString path;          // the directory with the "proton" script (PROTONPATH)
};

// the Steam installations of this user, native first, then ~/.steam/steam, then
// Flatpak; paths that lead to the same directory count once (the first spelling is kept)
QDLLEXPORT QStringList roots();

// the libraries of a Steam installation: the installation itself, then those of its
// steamapps/libraryfolders.vdf (absolute, existing, each once)
QDLLEXPORT QStringList libraries(const QString& root);

// the installed game with this app ID (digits only) in any library of any installation
QDLLEXPORT std::optional<App> findApp(const QString& appId);

// the Proton versions of every installation: the official ones
// (steamapps/common/Proton* of each library, with a "proton" script), then the
// compatibility tools (compatibilitytools.d/*/compatibilitytool.vdf); each directory once
QDLLEXPORT std::vector<Proton> installedProtons();

// the internal name of the compatibility tool Steam uses for the app:
// CompatToolMapping of config/config.vdf for the app ID, otherwise the default one
// (app ID "0"); empty when there is none
QDLLEXPORT QString compatToolName(const QString& appId);

// the installed Proton Steam uses for the app: the one named by compatToolName() if it
// is installed (nullopt if it is not); without a name, the one whose "proton" script
// has CURRENT_PREFIX_VERSION="<compatdata/version>" (the first in installedProtons()
// order); nullopt when none matches
QDLLEXPORT std::optional<Proton> protonForApp(const App& app);

// the Proton with this internal or display name (ignoring case) among `protons`
QDLLEXPORT std::optional<Proton> findProton(const std::vector<Proton>& protons,
                                            const QString& name);
}  // namespace MOBase::steam
```

- [ ] **Step 1: Testes (falham)**

`tests/test_linux_steam.cpp`: um HOME temporário com uma Steam falsa. Formatos copiados de uma instalação real (`appmanifest_489830.acf`, `libraryfolders.vdf`, `config.vdf` com `InstallConfigStore/Software/Valve/Steam/CompatToolMapping`, `compatibilitytool.vdf` do GE-Proton):

```cpp
#include <uibase/linux/steam.h>

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

using namespace MOBase;

namespace
{
void write(const QString& path, const QByteArray& content)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  ASSERT_TRUE(f.open(QIODevice::WriteOnly));
  f.write(content);
}

// HOME with a native Steam (~/.local/share/Steam) whose second library is under
// <tmp>/disk2; Skyrim SE (489830) is installed in disk2, its prefix in the main library
class LinuxSteam : public ::testing::Test
{
protected:
  void SetUp() override
  {
    m_oldHome = qgetenv("HOME");
    qputenv("HOME", QFile::encodeName(home()));
    m_steam = home() + "/.local/share/Steam";
    m_disk2 = m_tmp.path() + "/disk2";
    write(m_steam + "/steamapps/libraryfolders.vdf",
          ("\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" + m_steam +
           "\"\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" + m_disk2 +
           "\"\n\t\t\"apps\" { \"489830\" \"1\" }\n\t}\n}\n")
              .toUtf8());
    write(m_disk2 + "/steamapps/appmanifest_489830.acf",
          "\"AppState\"\n{\n\t\"appid\"\t\t\"489830\"\n\t\"installdir\"\t\t\"Skyrim "
          "Special Edition\"\n}\n");
    QDir().mkpath(m_disk2 + "/steamapps/common/Skyrim Special Edition");
    QDir().mkpath(m_steam + "/steamapps/compatdata/489830/pfx");
    write(m_steam + "/steamapps/compatdata/489830/version", "11.0-100\n");
    write(m_steam + "/steamapps/common/Proton - Experimental/proton",
          "#!/usr/bin/env python3\nCURRENT_PREFIX_VERSION=\"11.0-100\"\n");
    write(m_steam + "/steamapps/common/Proton 9.0/proton",
          "CURRENT_PREFIX_VERSION=\"9.0-200\"\n");
    // a runtime without a proton script is not a Proton
    QDir().mkpath(m_steam + "/steamapps/common/Proton EasyAntiCheat Runtime");
    write(m_steam + "/compatibilitytools.d/GE-Proton11-7-x86_64/proton",
          "CURRENT_PREFIX_VERSION=\"GE-Proton11-7\"\n");
    write(m_steam + "/compatibilitytools.d/GE-Proton11-7-x86_64/compatibilitytool.vdf",
          "\"compatibilitytools\"\n{\n  \"compat_tools\"\n  {\n    "
          "\"GE-Proton11-7-x86_64\" // Internal name\n    {\n      \"install_path\" "
          "\".\"\n      \"display_name\" \"GE-Proton11-7\"\n    }\n  }\n}\n");
  }

  void TearDown() override { qputenv("HOME", m_oldHome); }

  QString home() const { return m_tmp.path() + "/home"; }

  void mapping(const QByteArray& entries)
  {
    write(m_steam + "/config/config.vdf",
          "\"InstallConfigStore\"\n{\n\"Software\"\n{\n\"Valve\"\n{\n\"Steam\"\n{\n"
          "\"CompatToolMapping\"\n{\n" +
              entries + "}\n}\n}\n}\n}\n");
  }

  QTemporaryDir m_tmp;
  QByteArray m_oldHome;
  QString m_steam, m_disk2;
};
}  // namespace

TEST_F(LinuxSteam, RootsAreUniqueAndExisting)
{
  // ~/.steam/steam is a link to the native installation, as Steam makes it
  QDir().mkpath(home() + "/.steam");
  ASSERT_TRUE(QFile::link(m_steam, home() + "/.steam/steam"));
  EXPECT_EQ(steam::roots(), QStringList{m_steam});

  QDir().mkpath(home() + "/.var/app/com.valvesoftware.Steam/.local/share/Steam");
  EXPECT_EQ(steam::roots(),
            (QStringList{m_steam,
                         home() + "/.var/app/com.valvesoftware.Steam/.local/share/Steam"}));
}

TEST_F(LinuxSteam, Libraries)
{
  QDir().mkpath(m_disk2);
  EXPECT_EQ(steam::libraries(m_steam), (QStringList{m_steam, m_disk2}));
}

TEST_F(LinuxSteam, FindAppInOtherDiskWithPrefixInMainLibrary)
{
  const auto app = steam::findApp("489830");
  ASSERT_TRUE(app);
  EXPECT_EQ(app->root, m_steam);
  EXPECT_EQ(app->library, m_disk2);
  EXPECT_EQ(app->gamePath, m_disk2 + "/steamapps/common/Skyrim Special Edition");
  EXPECT_EQ(app->compatData, m_steam + "/steamapps/compatdata/489830");
}

TEST_F(LinuxSteam, PrefixInTheGameLibraryWins)
{
  QDir().mkpath(m_disk2 + "/steamapps/compatdata/489830/pfx");
  EXPECT_EQ(steam::findApp("489830")->compatData,
            m_disk2 + "/steamapps/compatdata/489830");
}

TEST_F(LinuxSteam, NoPrefixAndBadIds)
{
  QDir(m_steam + "/steamapps/compatdata/489830").removeRecursively();
  EXPECT_EQ(steam::findApp("489830")->compatData, "");
  EXPECT_FALSE(steam::findApp("1"));
  EXPECT_FALSE(steam::findApp("../489830"));
  EXPECT_FALSE(steam::findApp(""));
}

TEST_F(LinuxSteam, FlatpakSteam)
{
  const auto flatpak = home() + "/.var/app/com.valvesoftware.Steam/.local/share/Steam";
  QDir(m_steam).removeRecursively();
  write(flatpak + "/steamapps/appmanifest_489830.acf",
        "\"AppState\" { \"installdir\" \"Skyrim Special Edition\" }");
  QDir().mkpath(flatpak + "/steamapps/common/Skyrim Special Edition");
  QDir().mkpath(flatpak + "/steamapps/compatdata/489830/pfx");
  const auto app = steam::findApp("489830");
  ASSERT_TRUE(app);
  EXPECT_EQ(app->root, flatpak);
  EXPECT_EQ(app->compatData, flatpak + "/steamapps/compatdata/489830");
}

TEST_F(LinuxSteam, InstalledProtons)
{
  const auto protons = steam::installedProtons();
  ASSERT_EQ(protons.size(), 3u);
  EXPECT_EQ(protons[0].displayName, "Proton - Experimental");
  EXPECT_EQ(protons[0].internalName, "proton_experimental");
  EXPECT_EQ(protons[0].path, m_steam + "/steamapps/common/Proton - Experimental");
  EXPECT_EQ(protons[1].displayName, "Proton 9.0");
  EXPECT_EQ(protons[1].internalName, "proton_9");
  EXPECT_EQ(protons[2].displayName, "GE-Proton11-7");
  EXPECT_EQ(protons[2].internalName, "GE-Proton11-7-x86_64");
  EXPECT_EQ(protons[2].path, m_steam + "/compatibilitytools.d/GE-Proton11-7-x86_64");
  EXPECT_EQ(steam::findProton(protons, "ge-proton11-7")->internalName,
            "GE-Proton11-7-x86_64");
  EXPECT_FALSE(steam::findProton(protons, "proton_8"));
}

TEST_F(LinuxSteam, CompatToolOfTheAppThenTheDefault)
{
  mapping("\"0\" { \"name\" \"proton_9\" }\n"
          "\"489830\" { \"name\" \"GE-Proton11-7-x86_64\" \"config\" \"\" }\n");
  EXPECT_EQ(steam::compatToolName("489830"), "GE-Proton11-7-x86_64");
  EXPECT_EQ(steam::compatToolName("22330"), "proton_9");
  EXPECT_EQ(steam::protonForApp(*steam::findApp("489830"))->displayName,
            "GE-Proton11-7");
}

TEST_F(LinuxSteam, MappedProtonNotInstalled)
{
  mapping("\"489830\" { \"name\" \"proton_8\" }\n");
  EXPECT_FALSE(steam::protonForApp(*steam::findApp("489830")));
}

TEST_F(LinuxSteam, NoMappingUsesThePrefixVersion)
{
  EXPECT_EQ(steam::compatToolName("489830"), "");
  EXPECT_EQ(steam::protonForApp(*steam::findApp("489830"))->internalName,
            "proton_experimental");

  write(m_steam + "/steamapps/compatdata/489830/version", "1.0-1\n");
  EXPECT_FALSE(steam::protonForApp(*steam::findApp("489830")));
}
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `./build.sh --only uibase --tests`
Expected: FAIL de compilação (`uibase/linux/steam.h` não existe).

- [ ] **Step 3: Implementar `steam_linux.cpp`**

```cpp
// Steam installations, games, Proton prefixes and Proton versions
// (include/uibase/linux/steam.h), from the files of the Steam client.

#include "linux/steam.h"
#include "linux/vdf.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace MOBase::steam
{
namespace
{
  bool isAppId(const QString& s)
  {
    static const QRegularExpression re("^[0-9]+$");
    return re.match(s).hasMatch();
  }

  // "Proton - Experimental" -> proton_experimental, "Proton 9.0" -> proton_9,
  // "Proton 5.13" -> proton_513, "Proton Hotfix" -> proton_hotfix (Steam's names)
  QString officialInternalName(const QString& folder)
  {
    if (folder == "Proton - Experimental") {
      return "proton_experimental";
    }
    static const QRegularExpression version(R"(^Proton (\d+)\.(\d+)$)");
    if (const auto m = version.match(folder); m.hasMatch()) {
      return "proton_" + m.captured(1) + (m.captured(2) == "0" ? "" : m.captured(2));
    }
    QString name = folder.toLower();
    name.replace(QRegularExpression("[^a-z0-9]+"), "_");
    return name;
  }

  void addUnique(std::vector<Proton>& out, Proton p)
  {
    const auto canonical = QFileInfo(p.path).canonicalFilePath();
    for (const auto& q : out) {
      if (QFileInfo(q.path).canonicalFilePath() == canonical) {
        return;
      }
    }
    out.push_back(std::move(p));
  }
}  // namespace

QStringList roots()
{
  static const char* const candidates[] = {
      ".local/share/Steam",
      ".steam/steam",
      ".var/app/com.valvesoftware.Steam/.local/share/Steam",
  };

  QStringList result, canonical;
  const QDir home(QDir::homePath());

  for (const char* candidate : candidates) {
    const QString path = QDir::cleanPath(home.absoluteFilePath(candidate));
    const QString real = QFileInfo(path).canonicalFilePath();
    if (QFileInfo(path).isDir() && !canonical.contains(real)) {
      result.append(path);
      canonical.append(real);
    }
  }

  return result;
}

QStringList libraries(const QString& root)
{
  QStringList result{root}, canonical{QFileInfo(root).canonicalFilePath()};

  const auto file = vdf::parseFile(root + "/steamapps/libraryfolders.vdf");
  if (!file) {
    return result;
  }

  for (const auto& top : file->children) {
    for (const auto& entry : top.children) {
      bool numeric = false;
      entry.key.toUInt(&numeric);
      const QString path = QDir::cleanPath(entry.block ? entry.value(u"path")
                                           : numeric   ? entry.value
                                                       : QString());
      const QString real = QFileInfo(path).canonicalFilePath();
      if (!path.isEmpty() && QDir::isAbsolutePath(path) && QFileInfo(path).isDir() &&
          !canonical.contains(real)) {
        result.append(path);
        canonical.append(real);
      }
    }
  }

  return result;
}

std::optional<App> findApp(const QString& appId)
{
  if (!isAppId(appId)) {
    return {};
  }

  for (const auto& root : roots()) {
    for (const auto& library : libraries(root)) {
      const auto manifest =
          vdf::parseFile(library + "/steamapps/appmanifest_" + appId + ".acf");
      if (!manifest) {
        continue;
      }

      const auto installDir = manifest->path({u"AppState"})
                                  ? manifest->path({u"AppState"})->value(u"installdir")
                                  : QString();
      const auto gamePath = library + "/steamapps/common/" + installDir;
      if (installDir.isEmpty() || installDir.contains('/') ||
          !QFileInfo(gamePath).isDir()) {
        continue;
      }

      App app{appId, root, library, gamePath, {}};
      for (const auto& base : {library, root}) {
        const auto compat = base + "/steamapps/compatdata/" + appId;
        if (QFileInfo(compat + "/pfx").isDir()) {
          app.compatData = compat;
          break;
        }
      }
      return app;
    }
  }

  return {};
}

std::vector<Proton> installedProtons()
{
  std::vector<Proton> result;

  for (const auto& root : roots()) {
    for (const auto& library : libraries(root)) {
      QDir common(library + "/steamapps/common");
      for (const auto& name :
           common.entryList({"Proton*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const auto path = common.absoluteFilePath(name);
        if (QFileInfo(path + "/proton").isFile()) {
          addUnique(result, {name, officialInternalName(name), path});
        }
      }
    }
  }

  for (const auto& root : roots()) {
    QDir tools(root + "/compatibilitytools.d");
    for (const auto& dir :
         tools.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
      const auto manifestDir = tools.absoluteFilePath(dir);
      const auto manifest    = vdf::parseFile(manifestDir + "/compatibilitytool.vdf");
      const auto* compat =
          manifest ? manifest->path({u"compatibilitytools", u"compat_tools"}) : nullptr;
      if (!compat) {
        continue;
      }
      for (const auto& tool : compat->children) {
        if (!tool.block) {
          continue;
        }
        const auto install = tool.value(u"install_path");
        const auto path    = QDir::cleanPath(QDir(manifestDir).absoluteFilePath(
            install.isEmpty() ? QStringLiteral(".") : install));
        if (QFileInfo(path + "/proton").isFile()) {
          const auto display = tool.value(u"display_name");
          addUnique(result, {display.isEmpty() ? tool.key : display, tool.key, path});
        }
      }
    }
  }

  return result;
}

QString compatToolName(const QString& appId)
{
  QString fallback;

  for (const auto& root : roots()) {
    const auto config = vdf::parseFile(root + "/config/config.vdf");
    const auto* mapping =
        config ? config->path({u"InstallConfigStore", u"Software", u"Valve", u"Steam",
                               u"CompatToolMapping"})
               : nullptr;
    if (!mapping) {
      continue;
    }
    if (const auto* app = mapping->child(appId); app && !app->value(u"name").isEmpty()) {
      return app->value(u"name");
    }
    if (const auto* def = mapping->child(u"0"); def && fallback.isEmpty()) {
      fallback = def->value(u"name");
    }
  }

  return fallback;
}

std::optional<Proton> findProton(const std::vector<Proton>& protons, const QString& name)
{
  for (const auto& p : protons) {
    if (p.internalName.compare(name, Qt::CaseInsensitive) == 0 ||
        p.displayName.compare(name, Qt::CaseInsensitive) == 0) {
      return p;
    }
  }
  return {};
}

std::optional<Proton> protonForApp(const App& app)
{
  const auto protons = installedProtons();

  if (const auto name = compatToolName(app.appId); !name.isEmpty()) {
    return findProton(protons, name);
  }

  QFile versionFile(app.compatData + "/version");
  if (app.compatData.isEmpty() || !versionFile.open(QIODevice::ReadOnly)) {
    return {};
  }
  const auto version = versionFile.read(256).trimmed();
  if (version.isEmpty()) {
    return {};
  }
  const QByteArray needle = "CURRENT_PREFIX_VERSION=\"" + version + "\"";

  for (const auto& p : protons) {
    QFile script(p.path + "/proton");
    if (script.open(QIODevice::ReadOnly) && script.read(4 * 1024 * 1024).contains(needle)) {
      return p;
    }
  }

  return {};
}
}  // namespace MOBase::steam
```

Em `steamutility_linux.cpp`: `findSteam()` devolve `steam::roots().value(0)`, e `findSteamGame()` percorre `steam::libraries(findSteam())` em vez de montar a lista à mão. `parseLibraryFolders`/`libraryFolders` da Task 4 saem (ficam só em `steam::libraries`). O comportamento testado em `test_linux_utility.cpp` não muda.

CMake: header em `linux_headers`, `linux/steam_linux.cpp` nas fontes Linux, teste na lista Linux.

- [ ] **Step 4: Rodar**

Run: `./build.sh --only uibase --tests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add deps/uibase
git commit -m "deps/uibase: Linux: Steam apps, Proton prefixes and Proton versions"
```

---

### Task 6: `game_bethesda` — "My Games" e AppData dentro do prefixo

**Files:**
- Modify: `deps/game_bethesda/src/gamebryo/gamegamebryo.h` (`determineMyGamesPath` e `localAppFolder` deixam de ser `static`; `protonUserFolder()` Linux), `deps/game_bethesda/src/gamebryo/gamegamebryo.cpp` (blocos `#ifndef _WIN32` de 3 linhas), `deps/game_bethesda/src/gamebryo/linux/gamegamebryo_linux.cpp` (`protonUserFolder()`)
- Test: `deps/game_bethesda/tests/test_skyrimse_linux.cpp`

**Interfaces:**
- Consumes: Task 5 (`MOBase::steam::findApp`).
- Produces: no Linux, `IPluginGame::documentsDirectory()`/`savesDirectory()` do Skyrim SE e dos outros jogos Gamebryo apontam para `<compatdata>/pfx/drive_c/users/steamuser/Documents/My Games/<jogo>` quando o prefixo existe; `localAppFolder()` para `<...>/steamuser/AppData/Local`.

- [ ] **Step 1: Testes (falham)**

No fixture do `test_skyrimse_linux.cpp` (que já cria um HOME com a Steam nativa e uma segunda biblioteca), acrescentar o `appmanifest_489830.acf` na biblioteca do jogo e testes:

```cpp
TEST_F(SkyrimSE, DocumentsInTheProtonPrefix)
{
  const auto compat = m_steam + "/steamapps/compatdata/489830";
  QDir().mkpath(compat + "/pfx/drive_c/users/steamuser");
  auto game = load();  // the existing helper of the fixture that loads the plugin
  game->detectGame();
  EXPECT_EQ(game->documentsDirectory().absolutePath(),
            compat + "/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special "
                     "Edition");
  EXPECT_EQ(game->savesDirectory().absolutePath(),
            compat + "/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special "
                     "Edition/Saves");
}

TEST_F(SkyrimSE, DocumentsWithoutPrefixAreTheXdgOnes)
{
  auto game = load();
  game->detectGame();
  EXPECT_FALSE(game->documentsDirectory().absolutePath().contains("compatdata"));
}
```

Ajustar aos nomes reais do fixture (`m_steam`/biblioteca/helper de carga); se o fixture não tiver helper de carga, extrair um do teste `DetectsSteamInstall`. `localAppFolder()` é `protected`, então é verificado indiretamente pelos `mappings()` (o `plugins.txt` mapeado para `.../steamuser/AppData/Local/Skyrim Special Edition/plugins.txt`), se `mappings()` for acessível pelo `IPluginFileMapper`; senão, só pelo e2e (Task 12).

- [ ] **Step 2: Rodar e ver falhar**

Run: `./build.sh --only game_bethesda --tests`
Expected: FAIL (caminho XDG em vez do prefixo).

- [ ] **Step 3: Implementar**

Em `gamegamebryo.h`: tirar o `static` de `localAppFolder()` e de `determineMyGamesPath()` (todos os chamadores são métodos de instância: `detectGame()` e os overrides dos jogos; conferir com `grep -rn "determineMyGamesPath\|localAppFolder" deps/game_bethesda/src`). Acrescentar, numa seção `protected`:

```cpp
#ifndef _WIN32
  // <compatdata>/pfx/drive_c/users/steamuser of the Proton prefix of this game, or
  // empty when the game has no prefix (never run with Proton)
  QString protonUserFolder() const;
#endif
```

Em `gamegamebryo.cpp`, no começo de `determineMyGamesPath()` e de `localAppFolder()`:

```cpp
#ifndef _WIN32
  if (const auto user = protonUserFolder(); !user.isEmpty())
    return user + "/Documents/My Games/" + gameName;
#endif
```

```cpp
#ifndef _WIN32
  if (const auto user = protonUserFolder(); !user.isEmpty())
    return user + "/AppData/Local";
#endif
```

Em `linux/gamegamebryo_linux.cpp`:

```cpp
#include <uibase/linux/steam.h>
#include <uibase/log.h>

QString GameGamebryo::protonUserFolder() const
{
  const auto app = MOBase::steam::findApp(steamAPPId());
  if (!app || app->compatData.isEmpty()) {
    MOBase::log::warn("{}: no Proton prefix for Steam app {}, using the XDG folders",
                      gameName(), steamAPPId());
    return {};
  }
  return app->compatData + "/pfx/drive_c/users/steamuser";
}
```

e atualizar o comentário do topo do arquivo: "the known folders are the XDG folders..., except the documents and local application data of the game, which are in its Proton prefix (protonUserFolder())".

- [ ] **Step 4: Rodar**

Run: `./build.sh --only game_bethesda --tests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add deps/game_bethesda
git commit -m "deps/game_bethesda: Linux: documents and local app data in the Proton prefix"
```

---

### Task 7: MO2 — ponte para o host (`vfsbridge_linux`)

**Files:**
- Create: `deps/modorganizer/src/linux/vfsbridge_linux.h`, `deps/modorganizer/src/linux/vfsbridge_linux.cpp`, `deps/modorganizer/tests/test_vfsbridge.cpp`
- Modify: `deps/modorganizer/src/linux/vfsconnector_linux.cpp` (`start()`, `cleanupStaleMounts()`, `mountedRoots()` pela ponte), `deps/modorganizer/src/CMakeLists.txt` (`vfsbridge_linux.cpp` no `organizer_launch`), `deps/modorganizer/tests/CMakeLists.txt`, `deps/modorganizer/tests/test_vfsconnector.cpp` (`MO2_VFS_BRIDGE=direct` no `SetUp`)

**Interfaces:**
- Consumes: Task 2 (`mo2-vfs --list-mounts`).
- Produces:

```cpp
namespace vfsbridge
{
enum class Mode { Direct, Host };

// MO2_VFS_BRIDGE: "direct", "host", or empty/"auto" (anything else: a warning, then
// auto); auto is Host when <rootDir>/run/.containerenv or <rootDir>/.dockerenv exists
Mode mode(const QString& rootDir = "/");

struct Command
{
  QString program;
  QStringList arguments;
};

// how to run the helper `binary` with `args`: directly, or as
// `distrobox-host-exec <binary> <args>` in Host mode; throws std::runtime_error in Host
// mode when distrobox-host-exec is not in the PATH
Command helperCommand(const QString& binary, const QStringList& args,
                      const QString& rootDir = "/");
}  // namespace vfsbridge
```

- [ ] **Step 1: Testes (falham)**

`tests/test_vfsbridge.cpp`:

```cpp
#include "../src/linux/vfsbridge_linux.h"
#include "tempdir.h"

#include <gtest/gtest.h>

#include <QFile>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
struct Env
{
  explicit Env(const char* bridge)
      : m_old(qgetenv("MO2_VFS_BRIDGE")), m_oldPath(qgetenv("PATH"))
  {
    qputenv("MO2_VFS_BRIDGE", bridge);
  }
  ~Env()
  {
    qputenv("MO2_VFS_BRIDGE", m_old);
    qputenv("PATH", m_oldPath);
  }
  QByteArray m_old, m_oldPath;
};

QString q(const fs::path& p)
{
  return QString::fromStdString(p.string());
}
}  // namespace

TEST(VfsBridge, AutoDetectsContainers)
{
  Env e("");
  TempDir root;
  EXPECT_EQ(vfsbridge::mode(q(root.path())), vfsbridge::Mode::Direct);
  root.createFile("run/.containerenv");
  EXPECT_EQ(vfsbridge::mode(q(root.path())), vfsbridge::Mode::Host);

  TempDir docker;
  docker.createFile(".dockerenv");
  EXPECT_EQ(vfsbridge::mode(q(docker.path())), vfsbridge::Mode::Host);
}

TEST(VfsBridge, VariableWins)
{
  TempDir root;
  root.createFile("run/.containerenv");
  {
    Env e("direct");
    EXPECT_EQ(vfsbridge::mode(q(root.path())), vfsbridge::Mode::Direct);
  }
  {
    Env e("host");
    EXPECT_EQ(vfsbridge::mode("/nonexistent-root"), vfsbridge::Mode::Host);
  }
  {
    Env e("bogus");
    EXPECT_EQ(vfsbridge::mode(q(root.path())), vfsbridge::Mode::Host);
  }
}

TEST(VfsBridge, Commands)
{
  TempDir bin;
  const auto hostExec = bin.path() / "distrobox-host-exec";
  {
    std::ofstream(hostExec) << "#!/bin/sh\nexec \"$@\"\n";
  }
  fs::permissions(hostExec, fs::perms::owner_all);

  {
    Env e("direct");
    const auto c = vfsbridge::helperCommand("/opt/mo2/mo2-vfs", {"--cleanup"});
    EXPECT_EQ(c.program, "/opt/mo2/mo2-vfs");
    EXPECT_EQ(c.arguments, QStringList{"--cleanup"});
  }
  {
    Env e("host");
    qputenv("PATH", QFile::encodeName(q(bin.path())));
    const auto c = vfsbridge::helperCommand("/opt/mo2/mo2-vfs", {"--log-level", "info"});
    EXPECT_EQ(c.program, q(hostExec));
    EXPECT_EQ(c.arguments, (QStringList{"/opt/mo2/mo2-vfs", "--log-level", "info"}));
  }
  {
    Env e("host");
    qputenv("PATH", "/nonexistent");
    EXPECT_THROW(vfsbridge::helperCommand("/opt/mo2/mo2-vfs", {}), std::runtime_error);
  }
}
```

Em `test_vfsconnector.cpp`, guardar e restaurar `MO2_VFS_BRIDGE` no `SetUp`/`TearDown` como as outras variáveis, com `qputenv("MO2_VFS_BRIDGE", "direct")` no `SetUp`, e um teste da ponte de verdade com um `distrobox-host-exec` falso que só executa o comando:

```cpp
TEST_F(VfsConnectorTest, HostBridgeRunsTheHelperThroughDistroboxHostExec)
{
  if (!requireVfs())
    GTEST_SKIP();

  const auto bin      = m_state.path() / "bin";
  const auto hostExec = bin / "distrobox-host-exec";
  const auto marker   = m_state.path() / "bridge-used";
  fs::create_directories(bin);
  {
    std::ofstream(hostExec) << "#!/bin/sh\ntouch '" << marker.string()
                            << "'\nexec \"$@\"\n";
  }
  fs::permissions(hostExec, fs::perms::owner_all);
  const auto oldPath = qgetenv("PATH");
  qputenv("PATH", QFile::encodeName(q(bin)) + ":" + oldPath);
  qputenv("MO2_VFS_BRIDGE", "host");

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  {
    const auto vfs = createVfsConnector();
    Release release{*vfs};
    vfs->updateMapping(l.mapping());
    vfs->activate();
    EXPECT_TRUE(fs::exists(l.data / "a.esp"));
  }
  qputenv("PATH", oldPath);
  EXPECT_TRUE(fs::exists(marker));
}
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `./build.sh --only modorganizer --tests`
Expected: FAIL de compilação (`vfsbridge_linux.h` não existe).

- [ ] **Step 3: Implementar**

`vfsbridge_linux.cpp`:

```cpp
// How Mod Organizer starts mo2-vfs (vfsbridge_linux.h): directly, or on the host
// through distrobox-host-exec when it runs in a container, so that the mounts and the
// programs are those of the host (spec of Phase 3, §6.1: in the mo2-run container,
// /home and /var/home are two mounts and a FUSE mount covers only one of them).

#include "vfsbridge_linux.h"

#include <QFileInfo>
#include <QStandardPaths>
#include <stdexcept>

#include <uibase/log.h>

using namespace MOBase;

namespace vfsbridge
{
Mode mode(const QString& rootDir)
{
  const auto v = qEnvironmentVariable("MO2_VFS_BRIDGE");
  if (v == "direct") {
    return Mode::Direct;
  }
  if (v == "host") {
    return Mode::Host;
  }
  if (!v.isEmpty() && v != "auto") {
    log::warn("MO2_VFS_BRIDGE={} is not direct, host or auto; detecting", v);
  }

  const QString root = rootDir.endsWith('/') ? rootDir : rootDir + "/";
  const bool container =
      QFileInfo::exists(root + "run/.containerenv") || QFileInfo::exists(root + ".dockerenv");
  return container ? Mode::Host : Mode::Direct;
}

Command helperCommand(const QString& binary, const QStringList& args,
                      const QString& rootDir)
{
  if (mode(rootDir) == Mode::Direct) {
    return {binary, args};
  }

  const auto hostExec = QStandardPaths::findExecutable("distrobox-host-exec");
  if (hostExec.isEmpty()) {
    throw std::runtime_error(
        "cannot run programs on the host: Mod Organizer runs in a container and "
        "distrobox-host-exec was not found (set MO2_VFS_BRIDGE=direct to use the "
        "container's own mounts)");
  }

  return {hostExec, QStringList{binary} + args};
}
}  // namespace vfsbridge
```

Em `vfsconnector_linux.cpp`:
- `start()`: depois de conferir que o binário existe, `const auto cmd = vfsbridge::helperCommand(binary, args);` e `std::make_unique<VfsClient>(cmd.program, cmd.arguments)`. O `std::runtime_error` da ponte sobe como as outras falhas de `activate()`.
- `cleanupStaleMounts()`: `process.start(cmd.program, cmd.arguments)` com `cmd = vfsbridge::helperCommand(binary, {"--cleanup"})` dentro de um `try`; no `catch`, `log::warn("cannot clean up stale mounts: {}", e.what())` e `return {}`.
- `mountedRoots()`: deixa de ler `/proc/self/mountinfo`; roda `helperCommand(vfsBinary(), {"--list-mounts"})` com `QProcess` (mesmos timeouts do `--cleanup`: 5 s para iniciar, 10 s para terminar) e lê `roots` da linha JSON. Em falha, `log::warn` e lista vazia, como hoje quando o `mountinfo` não pode ser lido. `unescapeMountinfo()` sai (sem uso).
- Atualizar o comentário do topo do arquivo: "mo2-vfs is started through vfsbridge::helperCommand() (on the host when Mod Organizer is in a container); the mount table is the helper's (`--list-mounts`), not this process'".

CMake: `linux/vfsbridge_linux.cpp` (e o `.h`) na lista do `organizer_launch`; `test_vfsbridge.cpp` nas fontes de `organizer_tests` (o `CMakeLists.txt` dos testes lista por glob ou à mão; seguir o que estiver lá).

Smoke e e2e: em `tests/smoke/modorganizer-smoke.sh` e nos dois `env ...` de `tests/vfs-e2e/run.sh`, acrescentar `MO2_VFS_BRIDGE=direct` (o CI é um container). Commit separado, sem prefixo de subtree.

- [ ] **Step 4: Rodar**

Run: `./build.sh --only modorganizer --tests`
Expected: PASS (os testes do conector continuam passando com `direct`; o teste da ponte usa o `distrobox-host-exec` falso).

- [ ] **Step 5: Commit**

```bash
git add deps/modorganizer
git commit -m "deps/modorganizer: Linux: run mo2-vfs on the host from a container"
git add tests/smoke tests/vfs-e2e/run.sh
git commit -m "tests: run the helper directly in the CI container (MO2_VFS_BRIDGE=direct)"
```

---

### Task 8: MO2 — `VfsConnector::spawn()`, `runningPrograms()`, códigos de saída e um `update` por execução

**Files:**
- Create: `deps/modorganizer/src/linux/exitcodes_linux.h`, `deps/modorganizer/src/linux/exitcodes_linux.cpp`
- Modify: `deps/modorganizer/src/vfsconnector.h` (`VfsSpawnRequest`, `spawn()`, `runningPrograms()` com implementação padrão), `deps/modorganizer/src/linux/vfsclient_linux.{h,cpp}` (`exited`), `deps/modorganizer/src/linux/vfsconnector_linux.cpp`, `deps/modorganizer/src/CMakeLists.txt` (`exitcodes_linux.cpp` no `organizer_launch`)
- Test: `deps/modorganizer/tests/test_vfsconnector.cpp`

**Interfaces:**
- Consumes: Tasks 1, 2 e 7 (`spawn`, `exited`, `status.processes`; `vfsbridge::helperCommand`).
- Produces:

```cpp
// vfsconnector.h (shared)
// a program started by the helper of the virtual file system, where the helper runs
// (on the host when Mod Organizer runs in a container); Linux only
struct VfsSpawnRequest
{
  QStringList argv;         // argv[0] is looked up in the PATH of the helper
  QStringList environment;  // NAME=value, over the environment of the helper
  QString workingDirectory;
  QString winePrefix;       // the compatdata directory of a Proton program, or empty
  QString output;           // file for the output and errors of the program, or empty
};

class VfsConnector {
  ...
  // starts a program through the helper (started if needed, without mounting) and
  // returns its pid; while mounted, its process group keeps the file system mounted;
  // throws std::runtime_error with the reason when it can't start
  virtual qint64 spawn(const VfsSpawnRequest& request);  // default: throws "not supported"

  // the pids the virtual file system waits for: programs started by spawn() that still
  // run, and the Wine servers that keep it mounted (empty without a helper)
  virtual std::vector<qint64> runningPrograms() { return {}; }
};

// exitcodes_linux.h
namespace exitcodes
{
// the exit code of a program started through the helper (its exited event)
void record(qint64 pid, int code);

// the recorded exit code of pid, removed; waits up to `timeout` for it, running the
// event loop of this thread (the event arrives on it); nullopt if it doesn't come
std::optional<int> take(qint64 pid, std::chrono::milliseconds timeout);
}
```

- [ ] **Step 1: Testes (falham)**

Em `test_vfsconnector.cpp`:

```cpp
TEST_F(VfsConnectorTest, SpawnWithoutMountRunsAndRecordsExitCode)
{
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  TempDir t;
  const auto out = t.path() / "out.txt";

  VfsSpawnRequest r;
  r.argv             = {"sh", "-c", "echo \"$MO2_T\" > '" + q(out) + "'; exit 7"};
  r.environment      = {"MO2_T=hello"};
  r.workingDirectory = q(t.path());
  const auto pid     = vfs->spawn(r);
  ASSERT_GT(pid, 0);

  const auto code = exitcodes::take(pid, std::chrono::seconds(5));
  ASSERT_TRUE(code);
  EXPECT_EQ(*code, 7);
  EXPECT_EQ(readFile(out), "hello\n");
  EXPECT_FALSE(vfs->isActive());
}

TEST_F(VfsConnectorTest, SpawnFailureIsException)
{
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  VfsSpawnRequest r;
  r.argv             = {"/nonexistent/umu-run"};
  r.workingDirectory = "/";
  try {
    vfs->spawn(r);
    FAIL() << "no exception";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("/nonexistent/umu-run"), std::string::npos)
        << e.what();
  }
}

TEST_F(VfsConnectorTest, SpawnedProgramKeepsTheMountAndIsListed)
{
  if (!requireVfs())
    GTEST_SKIP();

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  Release release{*vfs};
  vfs->updateMapping(l.mapping());
  vfs->activate();

  VfsSpawnRequest r;
  r.argv             = {"sh", "-c", "ls > ../seen.txt; sleep 0.5"};
  r.workingDirectory = q(l.data);
  const auto pid     = vfs->spawn(r);

  EXPECT_EQ(vfs->runningPrograms(), std::vector<qint64>{pid});
  vfs->releaseIfIdle();  // a spawned program counts as watched: nothing happens
  EXPECT_TRUE(vfs->isActive());

  EXPECT_TRUE(vfs->waitInactive(std::chrono::seconds(5)));
  EXPECT_EQ(readFile(l.tmp.path() / "seen.txt"), "a.esp\nb.esp\noriginal.esm\ntextures\n");
  EXPECT_EQ(exitcodes::take(pid, std::chrono::seconds(2)), 0);
  EXPECT_TRUE(vfs->runningPrograms().empty());
}

TEST_F(VfsConnectorTest, ActivateAfterUpdateMappingUpdatesOnce)
{
  if (!requireVfs())
    GTEST_SKIP();

  Layout l;
  // counts the update requests by wrapping the real helper
  const auto log     = m_state.path() / "requests.log";
  const auto wrapper = m_state.path() / "wrapper.sh";
  {
    std::ofstream(wrapper) << "#!/bin/sh\n"
                           << "tee -a '" << log.string() << "' | '"
                           << vfsBinary().toStdString() << "' \"$@\"\n";
  }
  fs::permissions(wrapper, fs::perms::owner_all);
  qputenv("MO2_VFS_BINARY", QByteArray::fromStdString(wrapper.string()));

  const auto vfs = createVfsConnector();
  Release release{*vfs};
  vfs->updateMapping(l.mapping());
  vfs->activate();
  // the second Run: beforeRun() calls updateMapping() and then activate()
  vfs->updateMapping(l.mapping());
  vfs->activate();

  const auto requests = readFile(log);
  std::size_t updates = 0;
  for (auto p = requests.find("\"op\":\"update\""); p != std::string::npos;
       p          = requests.find("\"op\":\"update\"", p + 1)) {
    ++updates;
  }
  EXPECT_EQ(updates, 1u) << requests;
}
```

O teste `ActivateTwiceUpdates` existente continua valendo: lá o segundo `updateMapping` muda os mappings e o `update` acontece uma vez. O wrapper também atende `--cleanup` e `--list-mounts` (o `tee` só copia o stdin, que fica vazio nesses casos).

- [ ] **Step 2: Rodar e ver falhar**

Run: `./build.sh --only modorganizer --tests`
Expected: FAIL de compilação (`VfsSpawnRequest`, `exitcodes` inexistentes).

- [ ] **Step 3: Interface e registro de códigos**

`vfsconnector.h`: `VfsSpawnRequest` como em Interfaces; `#include <QStringList>`, `<stdexcept>`, `<vector>`. Implementação padrão:

```cpp
  virtual qint64 spawn(const VfsSpawnRequest& request)
  {
    Q_UNUSED(request);
    throw std::runtime_error("starting programs through the virtual file system is "
                             "not supported on this platform");
  }
```

`exitcodes_linux.cpp`:

```cpp
// Exit codes of the programs started through mo2-vfs (exitcodes_linux.h): they are not
// children of Mod Organizer, so waitpid() can't get them; the connector records them
// from the exited events, which arrive on the thread of the connector.

#include "exitcodes_linux.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <map>
#include <mutex>

namespace exitcodes
{
namespace
{
  std::mutex g_mutex;
  std::map<qint64, int> g_codes;

  std::optional<int> takeNow(qint64 pid)
  {
    std::scoped_lock lock(g_mutex);
    const auto it = g_codes.find(pid);
    if (it == g_codes.end()) {
      return {};
    }
    const int code = it->second;
    g_codes.erase(it);
    return code;
  }
}  // namespace

void record(qint64 pid, int code)
{
  std::scoped_lock lock(g_mutex);
  g_codes[pid] = code;
}

std::optional<int> take(qint64 pid, std::chrono::milliseconds timeout)
{
  QElapsedTimer elapsed;
  elapsed.start();

  for (;;) {
    if (auto code = takeNow(pid)) {
      return code;
    }
    if (elapsed.elapsed() >= timeout.count()) {
      return {};
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(10);
  }
}
}  // namespace exitcodes
```

- [ ] **Step 4: Cliente e conector**

`VfsClient`: `void setProgramExitedHandler(std::function<void(qint64, int)> f);` guardado no `State` como `programExited`. Em `onLine`, antes do `else` que guarda respostas:

```cpp
  } else if (name == "exited") {
    // spontaneous: a program started by spawn ended
    if (s.programExited) {
      s.programExited(event.value("pid").toInteger(), event.value("code").toInt());
    }
```

e o comentário da classe passa a citar o `exited` entre os eventos espontâneos.

`LinuxVfsConnector`:
- Em `start()`: `client->setProgramExitedHandler([](qint64 pid, int code) { log::debug("program {} ended with {}", pid, code); exitcodes::record(pid, code); });`.
- `spawn()`:

```cpp
  qint64 spawn(const VfsSpawnRequest& request) override
  {
    if (m_client && !m_client->running()) {
      helperLost("mo2-vfs is not running");
      m_client.reset();
    }
    if (!m_client) {
      // a program that runs without the virtual file system still runs where the
      // helper runs (the host)
      start();
    }

    QJsonObject env;
    for (const auto& e : request.environment) {
      const auto eq = e.indexOf('=');
      env[e.left(eq)] = e.mid(eq + 1);  // validated by the caller (ProtonLauncher)
    }

    QJsonObject r;
    r["op"]   = "spawn";
    r["argv"] = toJson(request.argv);
    r["env"]  = env;
    r["cwd"]  = request.workingDirectory;
    if (!request.winePrefix.isEmpty()) {
      r["wineprefix"] = request.winePrefix;
    }
    if (!request.output.isEmpty()) {
      r["output"] = request.output;
    }

    log::debug("spawn {} in '{}', env {}", request.argv.join(' '),
               request.workingDirectory, request.environment.join(' '));

    const auto reply = send(r, 10s);
    const auto pid   = reply.value("pid").toInteger();

    if (m_active) {
      // its process group keeps the mount: releaseIfIdle() must not unmount
      ++m_watched;
    }

    return pid;
  }

  std::vector<qint64> runningPrograms() override
  {
    if (!m_client || !m_client->running()) {
      return {};
    }
    try {
      std::vector<qint64> pids;
      for (const auto& p : send(QJsonObject{{"op", "status"}}, 10s)
                               .value("processes")
                               .toArray()) {
        pids.push_back(p.toInteger());
      }
      return pids;
    } catch (const std::exception& e) {
      log::error("cannot ask mo2-vfs for its programs: {}", e.what());
      return {};
    }
  }
```

- Um `update` por execução: campo `bool m_mappingSent = false;`. Em `updateMapping()`, quando ativo e o `update` foi enviado sem exceção, `m_mappingSent = true`; quando inativo, `m_mappingSent = false`. Em `doActivate()`, o ramo `if (m_active)` vira:

```cpp
    if (m_active) {
      if (!std::exchange(m_mappingSent, false)) {
        send(mappingRequest("update"), mountTimeout());
      }
      return;
    }
```

e depois do `mount` bem-sucedido, `m_mappingSent = false`.

CMake: `linux/exitcodes_linux.{h,cpp}` no `organizer_launch`.

- [ ] **Step 5: Rodar**

Run: `./build.sh --only modorganizer --tests`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add deps/modorganizer
git commit -m "deps/modorganizer: Linux: start programs through mo2-vfs, record exit codes"
```

---

### Task 9: MO2 — `ProtonLauncher` (argv/env/cwd do lançamento)

**Files:**
- Create: `deps/modorganizer/src/linux/protonlauncher_linux.h`, `deps/modorganizer/src/linux/protonlauncher_linux.cpp`, `deps/modorganizer/tests/test_protonlauncher.cpp`
- Modify: `deps/modorganizer/src/CMakeLists.txt` (no `organizer_launch`), `deps/modorganizer/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 5 (`MOBase::steam::*`), Task 8 (`VfsSpawnRequest`).
- Produces:

```cpp
namespace proton
{
// why a program can't be started; the message is for the user
class LaunchError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

struct LaunchParameters
{
  QFileInfo binary;
  QString arguments;         // one string, split like a command line
  QString workingDirectory;  // empty or "." for the directory of the binary
  QString steamAppId;        // of the executable; empty for the game's
  QString gameSteamAppId;    // of the managed game
  QString proton;            // internal or display name; empty for Steam's choice
  QStringList environment;   // NAME=value of the executable
  QString output;            // file for the output of the program, or empty
};

// .exe, .bat, .cmd and .com, ignoring case
bool isWindowsProgram(const QFileInfo& file);

// the NAME=value entries with a valid name ([A-Za-z_][A-Za-z0-9_]*); the others are
// logged and dropped
QStringList validEnvironment(const QStringList& environment);

// what to ask the helper to start: native programs directly, Windows programs through
// umu-run with the Steam and Proton environment of §6.2; throws LaunchError
VfsSpawnRequest makeLaunch(const LaunchParameters& p);
}  // namespace proton
```

- [ ] **Step 1: Testes (falham)**

`tests/test_protonlauncher.cpp` com a mesma Steam falsa da Task 5, reduzida (HOME temporário; Steam nativa com Skyrim SE `489830` e prefixo; Proton Experimental e GE-Proton; `config.vdf` opcional):

```cpp
#include "../src/linux/protonlauncher_linux.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace
{
void write(const QString& path, const QByteArray& content)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  ASSERT_TRUE(f.open(QIODevice::WriteOnly));
  f.write(content);
}

class ProtonLauncher : public ::testing::Test
{
protected:
  void SetUp() override
  {
    m_oldHome = qgetenv("HOME");
    qputenv("HOME", QFile::encodeName(m_tmp.path()));
    m_steam = m_tmp.path() + "/.local/share/Steam";
    m_game  = m_steam + "/steamapps/common/Skyrim Special Edition";
    write(m_steam + "/steamapps/appmanifest_489830.acf",
          "\"AppState\" { \"installdir\" \"Skyrim Special Edition\" }");
    write(m_game + "/SkyrimSE.exe", "MZ");
    write(m_game + "/tools/run me.bat", "@echo off");
    QDir().mkpath(m_steam + "/steamapps/compatdata/489830/pfx");
    write(m_steam + "/steamapps/compatdata/489830/version", "11.0-100");
    write(m_steam + "/steamapps/common/Proton - Experimental/proton",
          "CURRENT_PREFIX_VERSION=\"11.0-100\"");
    write(m_steam + "/compatibilitytools.d/GE-Proton11-7/proton", "x");
    write(m_steam + "/compatibilitytools.d/GE-Proton11-7/compatibilitytool.vdf",
          "\"compatibilitytools\" { \"compat_tools\" { \"GE-Proton11-7\" { "
          "\"install_path\" \".\" \"display_name\" \"GE-Proton11-7\" } } }");
  }

  void TearDown() override { qputenv("HOME", m_oldHome); }

  proton::LaunchParameters exe(const QString& rel, const QString& args = {})
  {
    proton::LaunchParameters p;
    p.binary         = QFileInfo(m_game + "/" + rel);
    p.arguments      = args;
    p.gameSteamAppId = "489830";
    return p;
  }

  QTemporaryDir m_tmp;
  QByteArray m_oldHome;
  QString m_steam, m_game;
};
}  // namespace

TEST_F(ProtonLauncher, WindowsExeArgv)
{
  const auto r = proton::makeLaunch(exe("SkyrimSE.exe"));
  EXPECT_EQ(r.argv, (QStringList{"umu-run", m_game + "/SkyrimSE.exe"}));
  EXPECT_EQ(r.workingDirectory, m_game);
  EXPECT_EQ(r.winePrefix, m_steam + "/steamapps/compatdata/489830");
  EXPECT_EQ(r.environment,
            (QStringList{"WINEPREFIX=" + m_steam + "/steamapps/compatdata/489830",
                         "GAMEID=umu-489830", "STORE=steam", "SteamAppId=489830",
                         "SteamGameId=489830",
                         "PROTONPATH=" + m_steam + "/steamapps/common/Proton - Experimental",
                         "STEAM_COMPAT_CLIENT_INSTALL_PATH=" + m_steam}));
}

TEST_F(ProtonLauncher, ArgumentsAreSplitLikeTheCommandLine)
{
  const auto r =
      proton::makeLaunch(exe("SkyrimSE.exe", R"(-forcesteamloader "a b" c)"));
  EXPECT_EQ(r.argv, (QStringList{"umu-run", m_game + "/SkyrimSE.exe",
                                 "-forcesteamloader", "a b", "c"}));
}

TEST_F(ProtonLauncher, BatchFilesRunThroughCmd)
{
  const auto r = proton::makeLaunch(exe("tools/run me.bat", "x"));
  EXPECT_EQ(r.argv,
            (QStringList{"umu-run", "cmd", "/c", m_game + "/tools/run me.bat", "x"}));
  EXPECT_EQ(r.workingDirectory, m_game + "/tools");
}

TEST_F(ProtonLauncher, ExecutableSettingsWin)
{
  auto p             = exe("SkyrimSE.exe");
  p.proton           = "ge-proton11-7";
  p.environment      = {"DXVK_HUD=fps", "WINEDLLOVERRIDES=dxgi=n,b"};
  p.workingDirectory = m_tmp.path();
  p.output           = "/tmp/x.log";
  const auto r       = proton::makeLaunch(p);
  EXPECT_TRUE(r.environment.contains("PROTONPATH=" + m_steam +
                                     "/compatibilitytools.d/GE-Proton11-7"));
  EXPECT_EQ(r.environment.mid(r.environment.size() - 2),
            (QStringList{"DXVK_HUD=fps", "WINEDLLOVERRIDES=dxgi=n,b"}));
  EXPECT_EQ(r.workingDirectory, m_tmp.path());
  EXPECT_EQ(r.output, "/tmp/x.log");
}

TEST_F(ProtonLauncher, ExecutableAppIdChoosesThePrefix)
{
  write(m_steam + "/steamapps/appmanifest_22330.acf",
        "\"AppState\" { \"installdir\" \"Oblivion\" }");
  QDir().mkpath(m_steam + "/steamapps/common/Oblivion");
  QDir().mkpath(m_steam + "/steamapps/compatdata/22330/pfx");
  auto p       = exe("SkyrimSE.exe");
  p.steamAppId = "22330";
  p.proton     = "GE-Proton11-7";
  const auto r = proton::makeLaunch(p);
  EXPECT_EQ(r.winePrefix, m_steam + "/steamapps/compatdata/22330");
  EXPECT_TRUE(r.environment.contains("SteamAppId=22330"));
}

TEST_F(ProtonLauncher, ConfiguredProtonMissing)
{
  auto p   = exe("SkyrimSE.exe");
  p.proton = "proton_8";
  try {
    proton::makeLaunch(p);
    FAIL();
  } catch (const proton::LaunchError& e) {
    const std::string m = e.what();
    EXPECT_NE(m.find("proton_8"), std::string::npos) << m;
    EXPECT_NE(m.find("GE-Proton11-7"), std::string::npos) << m;
  }
}

TEST_F(ProtonLauncher, SteamMappingProtonMissing)
{
  write(m_steam + "/config/config.vdf",
        "\"InstallConfigStore\" { \"Software\" { \"Valve\" { \"Steam\" { "
        "\"CompatToolMapping\" { \"489830\" { \"name\" \"proton_8\" } } } } } }");
  EXPECT_THROW(proton::makeLaunch(exe("SkyrimSE.exe")), proton::LaunchError);
}

TEST_F(ProtonLauncher, NoPrefixOrNoGame)
{
  QDir(m_steam + "/steamapps/compatdata").removeRecursively();
  EXPECT_THROW(proton::makeLaunch(exe("SkyrimSE.exe")), proton::LaunchError);

  auto p           = exe("SkyrimSE.exe");
  p.gameSteamAppId = "";
  EXPECT_THROW(proton::makeLaunch(p), proton::LaunchError);
}

TEST_F(ProtonLauncher, NativeProgramsRunDirectly)
{
  write(m_tmp.path() + "/tool.sh", "#!/bin/sh\n");
  proton::LaunchParameters p;
  p.binary      = QFileInfo(m_tmp.path() + "/tool.sh");
  p.arguments   = "a 'b c'";
  p.environment = {"X=1"};
  const auto r  = proton::makeLaunch(p);
  EXPECT_EQ(r.argv, (QStringList{m_tmp.path() + "/tool.sh", "a", "b c"}));
  EXPECT_EQ(r.environment, QStringList{"X=1"});
  EXPECT_EQ(r.workingDirectory, m_tmp.path());
  EXPECT_TRUE(r.winePrefix.isEmpty());
}

TEST_F(ProtonLauncher, InvalidEnvironmentEntriesAreDropped)
{
  EXPECT_EQ(proton::validEnvironment(
                {"FOO", "=x", "1A=b", "", "  ", "OK=1", "_U=", "A_B9=x=y"}),
            (QStringList{"OK=1", "_U=", "A_B9=x=y"}));
}

TEST_F(ProtonLauncher, WindowsProgramTypes)
{
  for (const auto* n : {"a.exe", "a.EXE", "a.bat", "a.Cmd", "a.com"}) {
    EXPECT_TRUE(proton::isWindowsProgram(QFileInfo(n))) << n;
  }
  for (const auto* n : {"a.sh", "a", "a.exe.txt", "a.jar"}) {
    EXPECT_FALSE(proton::isWindowsProgram(QFileInfo(n))) << n;
  }
}
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `./build.sh --only modorganizer --tests`
Expected: FAIL de compilação.

- [ ] **Step 3: Implementar**

```cpp
// Launch of the programs started by Run on Linux (protonlauncher_linux.h): Windows
// programs through umu-run in the Proton prefix of their Steam game (spec of Phase 3,
// §6.2), native programs directly; both are then started by mo2-vfs
// (VfsConnector::spawn()).

#include "protonlauncher_linux.h"

#include <QObject>
#include <QProcess>
#include <QRegularExpression>

#include <uibase/linux/steam.h>
#include <uibase/log.h>

using namespace MOBase;

namespace proton
{
namespace
{
  QString names(const std::vector<steam::Proton>& protons)
  {
    QStringList list;
    for (const auto& p : protons) {
      list.append(p.displayName);
    }
    return list.isEmpty() ? QObject::tr("none") : list.join(", ");
  }
}  // namespace

bool isWindowsProgram(const QFileInfo& file)
{
  static const QStringList suffixes = {"exe", "bat", "cmd", "com"};
  return suffixes.contains(file.suffix(), Qt::CaseInsensitive);
}

QStringList validEnvironment(const QStringList& environment)
{
  static const QRegularExpression name("^[A-Za-z_][A-Za-z0-9_]*$");
  QStringList valid;

  for (const auto& e : environment) {
    const auto eq = e.indexOf('=');
    if (eq > 0 && name.match(e.left(eq)).hasMatch()) {
      valid.append(e);
    } else if (!e.trimmed().isEmpty()) {
      log::warn("ignoring the environment variable '{}' of the executable: it is not "
                "NAME=value",
                e);
    }
  }

  return valid;
}

VfsSpawnRequest makeLaunch(const LaunchParameters& p)
{
  VfsSpawnRequest r;
  const auto program = p.binary.absoluteFilePath();
  const auto args    = QProcess::splitCommand(p.arguments);

  r.workingDirectory = (p.workingDirectory.isEmpty() || p.workingDirectory == ".")
                           ? p.binary.absolutePath()
                           : p.workingDirectory;
  r.output           = p.output;
  const auto extra   = validEnvironment(p.environment);

  if (!isWindowsProgram(p.binary)) {
    r.argv        = QStringList{program} + args;
    r.environment = extra;
    return r;
  }

  const auto appId = p.steamAppId.isEmpty() ? p.gameSteamAppId : p.steamAppId;
  if (appId.isEmpty()) {
    throw LaunchError(QObject::tr("Cannot start \"%1\" with Proton: the game has no "
                                  "Steam app ID; set one in the executable settings.")
                          .arg(program)
                          .toStdString());
  }

  const auto app = steam::findApp(appId);
  if (!app) {
    throw LaunchError(QObject::tr("Cannot start \"%1\" with Proton: the Steam game %2 "
                                  "is not installed in a Steam library.")
                          .arg(program, appId)
                          .toStdString());
  }
  if (app->compatData.isEmpty()) {
    throw LaunchError(QObject::tr("Cannot start \"%1\": the Steam game %2 has no Proton "
                                  "prefix yet. Run the game once from Steam to create "
                                  "it.")
                          .arg(program, appId)
                          .toStdString());
  }

  const auto protons = steam::installedProtons();
  std::optional<steam::Proton> proton;

  if (!p.proton.isEmpty()) {
    proton = steam::findProton(protons, p.proton);
    if (!proton) {
      throw LaunchError(QObject::tr("Proton \"%1\", set for this executable, is not "
                                    "installed. Installed: %2.")
                            .arg(p.proton, names(protons))
                            .toStdString());
    }
  } else {
    proton = steam::protonForApp(*app);
    if (!proton) {
      const auto mapped = steam::compatToolName(appId);
      throw LaunchError(
          (mapped.isEmpty()
               ? QObject::tr("Cannot tell which Proton Steam uses for the game %1; "
                             "choose one in the executable settings. Installed: %2.")
                     .arg(appId, names(protons))
               : QObject::tr("Steam uses Proton \"%1\" for the game %2, but it is not "
                             "installed. Installed: %3.")
                     .arg(mapped, appId, names(protons)))
              .toStdString());
    }
  }

  const bool batch = p.binary.suffix().compare("bat", Qt::CaseInsensitive) == 0 ||
                     p.binary.suffix().compare("cmd", Qt::CaseInsensitive) == 0;
  r.argv = batch ? QStringList{"umu-run", "cmd", "/c", program} + args
                 : QStringList{"umu-run", program} + args;

  r.environment = QStringList{"WINEPREFIX=" + app->compatData,
                              "GAMEID=umu-" + appId,
                              "STORE=steam",
                              "SteamAppId=" + appId,
                              "SteamGameId=" + appId,
                              "PROTONPATH=" + proton->path,
                              "STEAM_COMPAT_CLIENT_INSTALL_PATH=" + app->root} +
                  extra;
  r.winePrefix = app->compatData;

  log::debug("{} runs with {} in {}", program, proton->displayName, app->compatData);
  return r;
}
}  // namespace proton
```

CMake: no `organizer_launch` (já linka o `uibase`), teste nas fontes de `organizer_tests`.

- [ ] **Step 4: Rodar**

Run: `./build.sh --only modorganizer --tests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add deps/modorganizer
git commit -m "deps/modorganizer: Linux: Proton launch of Windows programs"
```

---

### Task 10: MO2 — Proton e variáveis por executável (`ExecutablesList`, diálogo)

**Files:**
- Create: `deps/modorganizer/src/linux/protonsettings_linux.h`, `deps/modorganizer/src/linux/protonsettings_linux.cpp`
- Modify: `deps/modorganizer/src/executableslist.{h,cpp}` (campos `proton`, `environment`), `deps/modorganizer/src/spawn.h` (`SpawnParameters::proton`, `environment`), `deps/modorganizer/src/processrunner.cpp` (copia os campos do executável), `deps/modorganizer/src/editexecutablesdialog.{h,cpp}` (grupo "Proton", só Linux)

**Interfaces:**
- Consumes: Task 5 (`steam::installedProtons()`).
- Produces: `Executable::proton() -> const QString&`, `Executable::proton(const QString&) -> Executable&`, `Executable::environment() -> const QStringList&`, `Executable::environment(const QStringList&) -> Executable&`; chaves `proton` e `environment` no mapa do executável (`ModOrganizer.ini`, `[customExecutables]`); `SpawnParameters::proton` (`QString`) e `SpawnParameters::environment` (`QStringList`), preenchidos a partir do executável.

- [ ] **Step 1: Campos (código compartilhado, sem API nova do `uibase`)**

`executableslist.h`: membros `QString m_proton; QStringList m_environment;`, os getters/setters de Interfaces, cópia em `mergeFrom()` e no construtor de cópia/atribuição, se houver um explícito (seguir `m_steamAppID` em todo lugar onde aparece: `grep -n m_steamAppID deps/modorganizer/src/executableslist.*`).

`executableslist.cpp`, `load` (junto de `.steamAppID(...)`):

```cpp
                      .proton(map["proton"].toString())
                      .environment(map["environment"].toStringList())
```

`store`:

```cpp
    map["proton"]               = item.proton();
    map["environment"]          = item.environment();
```

`spawn.h`, em `SpawnParameters`:

```cpp
  QString proton;           // Linux: Proton of the executable (empty: Steam's choice)
  QStringList environment;  // Linux: NAME=value added to the environment
```

`processrunner.cpp`: logo após cada `setSteamID(exe.steamAppID());` (`grep -n "setSteamID(exe.steamAppID())" deps/modorganizer/src/processrunner.cpp`), uma linha:

```cpp
  m_sp.proton      = exe.proton();
  m_sp.environment = exe.environment();
```

(duas linhas, válidas nas duas plataformas: os campos existem em ambas e o Windows os ignora).

- [ ] **Step 2: Widget Linux**

`linux/protonsettings_linux.h`:

```cpp
#ifndef PROTONSETTINGS_LINUX_H
#define PROTONSETTINGS_LINUX_H

#include <QGroupBox>

class Executable;
class QComboBox;
class QPlainTextEdit;

// the "Proton" group of the executables dialog on Linux: the Proton version
// ("Automatic" is Steam's choice) and the environment variables, one NAME=value per
// line
class ProtonSettingsWidget : public QGroupBox
{
  Q_OBJECT

public:
  explicit ProtonSettingsWidget(QWidget* parent = nullptr);

  void setFrom(const Executable& e);
  void saveTo(Executable& e) const;

  // nothing selected in the list
  void clearAndDisable();

signals:
  void changed();

private:
  QComboBox* m_proton;
  QPlainTextEdit* m_environment;

  void fillProtons(const QString& current);
};

#endif
```

`linux/protonsettings_linux.cpp`:

```cpp
// The Proton settings of an executable in the executables dialog (Linux).

#include "protonsettings_linux.h"
#include "../executableslist.h"

#include <QComboBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QSignalBlocker>

#include <uibase/linux/steam.h>

ProtonSettingsWidget::ProtonSettingsWidget(QWidget* parent)
    : QGroupBox(tr("Proton"), parent), m_proton(new QComboBox(this)),
      m_environment(new QPlainTextEdit(this))
{
  auto* form = new QFormLayout(this);
  form->addRow(tr("Version"), m_proton);
  form->addRow(tr("Environment"), m_environment);
  m_environment->setPlaceholderText(tr("NAME=value, one per line"));
  m_environment->setMaximumHeight(80);
  setToolTip(tr("Used for Windows programs, which run with Proton in the prefix of "
                "their Steam game."));

  connect(m_proton, &QComboBox::currentIndexChanged, this, &ProtonSettingsWidget::changed);
  connect(m_environment, &QPlainTextEdit::textChanged, this, &ProtonSettingsWidget::changed);
}

void ProtonSettingsWidget::fillProtons(const QString& current)
{
  const QSignalBlocker block(m_proton);
  m_proton->clear();
  m_proton->addItem(tr("Automatic (same as Steam)"), QString());

  for (const auto& p : MOBase::steam::installedProtons()) {
    m_proton->addItem(p.displayName, p.internalName);
  }

  if (current.isEmpty()) {
    m_proton->setCurrentIndex(0);
    return;
  }

  int index = m_proton->findData(current, Qt::UserRole, Qt::MatchFixedString);
  if (index < 0) {
    index = m_proton->findText(current, Qt::MatchFixedString);
  }
  if (index < 0) {
    m_proton->addItem(tr("%1 (not installed)").arg(current), current);
    index = m_proton->count() - 1;
  }
  m_proton->setCurrentIndex(index);
}

void ProtonSettingsWidget::setFrom(const Executable& e)
{
  setEnabled(true);
  fillProtons(e.proton());
  const QSignalBlocker block(m_environment);
  m_environment->setPlainText(e.environment().join('\n'));
}

void ProtonSettingsWidget::saveTo(Executable& e) const
{
  e.proton(m_proton->currentData().toString());

  QStringList env;
  for (const auto& line : m_environment->toPlainText().split('\n')) {
    if (!line.trimmed().isEmpty()) {
      env.append(line.trimmed());
    }
  }
  e.environment(env);
}

void ProtonSettingsWidget::clearAndDisable()
{
  const QSignalBlocker b1(m_proton), b2(m_environment);
  m_proton->clear();
  m_environment->clear();
  setEnabled(false);
}
```

`editexecutablesdialog.h`: sob `#ifndef _WIN32`, `class ProtonSettingsWidget;` antes da classe e o membro `ProtonSettingsWidget* m_proton = nullptr;`. `editexecutablesdialog.cpp`, cada mudança em `#ifndef _WIN32` de até 3 linhas:
- include: `#include "linux/protonsettings_linux.h"`;
- no construtor, depois dos `connect` existentes:

```cpp
#ifndef _WIN32
  m_proton = new ProtonSettingsWidget(this);
  ui->verticalLayout_5->insertWidget(ui->verticalLayout_5->indexOf(ui->horizontalLayout_5) + 1, m_proton);
  connect(m_proton, &ProtonSettingsWidget::changed, [&] { save(); });
#endif
```

- `setEdits()`: `m_proton->setFrom(e);`; `clearEdits()`: `m_proton->clearAndDisable();`; `save()`, junto do `steamAppID`: `m_proton->saveTo(*e);`.

O `protonsettings_linux.cpp` é compilado no app (não no `organizer_launch`): conferir no `src/CMakeLists.txt` como os outros arquivos `linux/` do app entram (o filtro da linha ~135) e incluí-lo do mesmo jeito; o `AUTOMOC` do app cobre o `Q_OBJECT`.

- [ ] **Step 3: Compilar e conferir à mão**

Run: `./build.sh --only modorganizer --tests`
Expected: PASS (nenhum teste novo aqui: `ExecutablesList` e o diálogo são código do app, fora do `organizer_tests`. A leitura das chaves é coberta pelo e2e da Task 12, e o diálogo pelo checklist manual da Task 13).

Conferir também que o Windows não é afetado: `git diff deps/modorganizer/src/editexecutablesdialog.cpp` só tem linhas dentro de `#ifndef _WIN32`, e `spawn.h`/`executableslist.*`/`processrunner.cpp` só usam Qt.

- [ ] **Step 4: Commit**

```bash
git add deps/modorganizer
git commit -m "deps/modorganizer: Proton and environment of executables (Linux UI)"
```

---

### Task 11: MO2 — "Executar" pelo helper (`processrunner_linux.cpp`)

**Files:**
- Modify: `deps/modorganizer/src/linux/processrunner_linux.cpp` (`run()`, `waitForProcess()`, `waitForAllUSVFSProcessesWithLock()`, comentário do topo), `deps/modorganizer/src/linux/spawn_linux.cpp` (mensagem de `.exe` fora do "Executar", comentário do topo), `deps/modorganizer/src/linux/loot_linux.cpp` (comentários "before Phase 3" → "Phase 4"), `deps/modorganizer/src/linux/README.md`
- Test: `deps/modorganizer/tests/test_launch.cpp`

**Interfaces:**
- Consumes: Task 8 (`m_core.vfs().spawn()`, `runningPrograms()`, `exitcodes::take()`), Task 9 (`proton::makeLaunch()`), Task 10 (`m_sp.proton`, `m_sp.environment`). A dica do `umu-launcher` (§7) é conferida no checklist manual (item 5 da Task 13), porque o app não tem teste unitário.
- Produces: "Executar" (botão, menu e `ModOrganizer run`) inicia programas nativos e Windows pelo helper; `ProcessRunner::exitCode()` vem do `exited`.

- [ ] **Step 1: Teste da mensagem (falha)**

Em `test_launch.cpp`, trocar `ExeStillShowsPhase3Message` por:

```cpp
TEST(Launch, UnavailableMessageNoLongerMentionsPhase3)
{
  const auto m = spawn::launchUnavailableMessage();
  EXPECT_FALSE(m.contains("Fase 3"));
  EXPECT_FALSE(m.contains("Phase 3"));
  EXPECT_TRUE(m.contains("Run"));
}
```

e ajustar `UnavailableMessage` (linha 51) ao texto novo.

- [ ] **Step 2: Rodar e ver falhar**

Run: `./build.sh --only modorganizer --tests`
Expected: FAIL (`launchUnavailableMessage()` ainda é a mensagem da Fase 3).

- [ ] **Step 3: `spawn_linux.cpp`**

```cpp
QString launchUnavailableMessage()
{
  return QObject::tr("Windows programs are started with Run (or the executables list) "
                     "on Linux, which uses Proton.");
}
```

`startBinary()` continua só com programas nativos (o único outro chamador é o LOOT, da Fase 4). Atualizar o comentário do topo: Windows programs são iniciados por `ProcessRunner::run()` através do `mo2-vfs` (`processrunner_linux.cpp`); `startBinary()` mostra esta mensagem para eles.

- [ ] **Step 4: `run()` pelo helper**

Em `processrunner_linux.cpp`, incluir `"exitcodes_linux.h"`, `"protonlauncher_linux.h"`, `"../shared/appconfig.h"`, `<QCoreApplication>`. Substituir o trecho de `run()` desde o `if (shouldRunShell() || !spawn::isNativeProgram(m_sp.binary))` até o `return postRun();` final por:

```cpp
  QWidget* parent = (m_ui ? m_ui->mainWindow() : nullptr);

  // without a user interface (the run command) a dialog would never be answered
  auto showError = [&](const QString& text) {
    log::error("{}", text);
    if (m_ui) {
      QMessageBox::critical(parent, QObject::tr("Mod Organizer"), text);
    }
  };

  if (shouldRunShell()) {
    // setFromFile() keeps only documents for the shell, handled above
    showError(QObject::tr("Cannot start \"%1\".").arg(m_shellOpen.absoluteFilePath()));
    return Error;
  }

  // the output of the program, next to the logs of Mod Organizer (none in the tests)
  QString output;
  if (const auto dp = qApp->property("dataPath"); !dp.isNull()) {
    output = dp.toString() + "/" + QString::fromStdWString(AppConfig::logPath()) +
             "/run-" + m_sp.binary.completeBaseName() + ".log";
  }

  VfsSpawnRequest request;
  try {
    const auto* game = m_core.managedGame();
    request          = proton::makeLaunch({m_sp.binary, m_sp.arguments,
                                           m_sp.currentDirectory.path(), m_sp.steamAppID,
                                           game ? game->steamAPPId() : QString(),
                                           m_sp.proton, m_sp.environment, output});
  } catch (const proton::LaunchError& e) {
    showError(QString::fromStdString(e.what()));
    return Error;
  }

  if (m_sp.hooked) {
    // (the existing profile name and beforeRun() block, unchanged)
  }

  qint64 pid = 0;
  try {
    pid = m_core.vfs().spawn(request);
  } catch (const std::exception& e) {
    auto text = QObject::tr("Cannot start \"%1\": %2")
                    .arg(m_sp.binary.absoluteFilePath(), QString::fromStdString(e.what()));
    if (request.argv.value(0) == "umu-run" &&
        QString::fromStdString(e.what()).contains("umu-run")) {
      // spawn_failed for argv[0]: umu-run is not in the PATH of the host
      text += "\n\n" + QObject::tr("Windows programs run with umu-run: install "
                                    "umu-launcher on the host.");
    }
    showError(text);
    if (m_sp.hooked) {
      // nothing will end the group that keeps the file system mounted
      m_core.vfs().releaseIfIdle();
    }
    return Error;
  }

  log::debug("started '{}', pid {}", m_sp.binary.absoluteFilePath(), pid);
  m_handle.reset(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(pid)));

  return postRun();
```

Observações para o implementador:
- `m_sp.currentDirectory.path()` de um `QDir` vazio é `"."`, que o `makeLaunch` troca pela pasta do binário (pendência "diretório de trabalho vazio usa o cwd do MO2").
- O bloco `if (m_sp.hooked) { ... beforeRun ... }` existente fica como está, entre o `makeLaunch` e o `spawn`: um erro de Proton não monta nada.
- O `watchProcessGroup()` sai: o helper já acompanha o grupo do `spawn`.
- `showError` só abre diálogo com interface, como o `beforeRun` da Fase 2.

- [ ] **Step 5: Código de saída e espera de todos**

Em `waitForProcess()`, trocar o `else { log::warn(...) }` do `waitpid` por:

```cpp
    } else if (const auto code = exitcodes::take(toPid(initialProcess),
                                                 std::chrono::seconds(2))) {
      // started by mo2-vfs: not a child of this process
      *exitCode = static_cast<DWORD>(*code);
    } else {
      log::warn("failed to get exit code of process {}",
                static_cast<int>(toPid(initialProcess)));
    }
```

`waitForAllUSVFSProcessesWithLock()`, igual ao do Windows trocando a lista do USVFS pela do helper:

```cpp
ProcessRunner::Results
ProcessRunner::waitForAllUSVFSProcessesWithLock(UILocker::Reasons reason)
{
  m_lockReason = reason;

  if (!m_core.settings().interface().lockGUI()) {
    // disabling locking is like clicking on unlock immediately
    return ForceUnlocked;
  }

  auto r = Error;

  for (;;) {
    withLock([&](auto& ls) {
      // the programs started through mo2-vfs and the Wine servers that keep its mount
      std::vector<HANDLE> processes;
      for (const auto pid : m_core.vfs().runningPrograms()) {
        processes.push_back(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(pid)));
      }

      if (processes.empty()) {
        r = Completed;
        return;
      }

      r = waitForProcesses(processes, &ls);

      if (r != Completed) {
        // error, cancelled, or unlocked
        return;
      }

      // this process is completed, check for others
      r = Running;
    });

    if (r != Running) {
      break;
    }
  }

  return r;
}
```

Reescrever o comentário do topo do arquivo para o fluxo novo (ProtonLauncher, `spawn`, espera pela árvore do pid em `/proc` com o PID compartilhado do container, código pelo `exited`, `waitForAllUSVFSProcessesWithLock` pelos `processes` do helper), tirando as frases "not started before Phase 3".

`loot_linux.cpp`: os dois comentários "before Phase 3" passam a dizer que o LOOT continua desativado até a Fase 4. `src/linux/README.md`: atualizar o parágrafo de `processrunner_linux.cpp` e a lista do `organizer_launch` (`vfsbridge_linux.cpp`, `exitcodes_linux.cpp`, `protonlauncher_linux.cpp`), e citar `protonsettings_linux.cpp` entre os arquivos do app.

- [ ] **Step 6: Rodar**

Run: `./build.sh --only modorganizer --tests`
Expected: PASS (o e2e da Fase 2 continua passando: o probe nativo agora sai pelo `spawn`).

- [ ] **Step 7: Commit**

```bash
git add deps/modorganizer
git commit -m "deps/modorganizer: Linux: Run starts Windows programs with Proton"
```

---

### Task 12: e2e — "Executar" de um `.exe` com um `umu-run` falso

**Files:**
- Create: `tests/vfs-e2e/umu-run` (falso), `tests/fixtures/vfs-e2e/game/probe.exe`
- Modify: `tests/vfs-e2e/run.sh`, `tests/lib/instance.sh`, `tests/fixtures/vfs-e2e/instance/ModOrganizer.ini`

**Interfaces:**
- Consumes: Tasks 1–11 (app e helper instalados por `./build.sh`).
- Produces: o e2e cobre o fluxo inteiro de um `.exe`: descoberta da Steam, prefixo, Proton por executável, variáveis do executável, argv/env do `umu-run`, VFS montado durante o "jogo" e desmontado ao fim.

- [ ] **Step 1: Steam falsa no HOME e o jogo dentro dela**

`instance.sh`: `mo2_make_instance` passa a receber também `<steam root>` como 6º argumento (opcional; vazio mantém o comportamento atual, usado pelo smoke) e, quando dado:
- copia o jogo para `<steam root>/steamapps/common/Skyrim Special Edition` (o `<game dir>` passado deve ser esse caminho);
- escreve `<steam root>/steamapps/appmanifest_489830.acf` (`"AppState" { "appid" "489830" "installdir" "Skyrim Special Edition" }`);
- cria `<steam root>/steamapps/compatdata/489830/pfx/drive_c/users/steamuser/{Documents/My Games/Skyrim Special Edition,AppData/Local/Skyrim Special Edition}` e `compatdata/489830/version` (`11.0-100`);
- cria `<steam root>/compatibilitytools.d/GE-Proton11-7/{proton,compatibilitytool.vdf}` (o `proton` com `CURRENT_PREFIX_VERSION="GE-Proton11-7"`), como na Task 9;
- substitui `@PROBE_EXE@` por `<game dir>/probe.exe`.

`run.sh`: `GAME="$HOMEDIR/.local/share/Steam/steamapps/common/Skyrim Special Edition"` e `mo2_make_instance ... "$HOMEDIR/.local/share/Steam"`.

`ModOrganizer.ini` do fixture, segundo executável:

```ini
[customExecutables]
size=2
1\title=probe
...
2\title=probe-exe
2\binary=@PROBE_EXE@
2\arguments=@GAME_PATH@/Data "two words"
2\workingDirectory=
2\toolbar=false
2\hide=false
2\ownicon=false
2\steamAppID=
2\proton=GE-Proton11-7
2\environment=MO2_E2E_VAR=hello
```

`probe.exe` do fixture: um arquivo qualquer (`MZ`), nunca executado de verdade.

- [ ] **Step 2: `umu-run` falso**

`tests/vfs-e2e/umu-run`:

```bash
#!/usr/bin/env bash
# Fake umu-run of the end-to-end test: records how Mod Organizer (through mo2-vfs)
# started it, then runs probe.sh on the data directory given as the first argument of
# the "Windows program", like the game would see it.
set -u
OUT="${UMU_OUT:?UMU_OUT is not set}"
{
    printf 'arg %s\n' "$@"
    for v in WINEPREFIX GAMEID STORE SteamAppId SteamGameId PROTONPATH \
        STEAM_COMPAT_CLIENT_INSTALL_PATH MO2_E2E_VAR; do
        printf 'env %s=%s\n' "$v" "${!v-<unset>}"
    done
    printf 'cwd %s\n' "$PWD"
} > "$OUT"
# $1 is the .exe, $2 the data directory, $3 "two words"
exec "$PROBE" "$2"
```

- [ ] **Step 3: Cenário novo no `run.sh`**

Depois do cenário do probe nativo e antes do helper que falha: instalar o `umu-run` falso em `$WORK/umu-bin`, rodar `ModOrganizer -i vfs-e2e run -e probe-exe` com o mesmo `env` do primeiro cenário mais `PATH="$WORK/umu-bin:$PATH" UMU_OUT="$WORK/umu.out" PROBE="$WORK/bin/probe.sh" MO2_VFS_BRIDGE=direct`, e conferir:

```bash
[[ -s $WORK/umu.out ]] || die "umu-run was not started"
cat "$WORK/umu.out"
expect_line() { grep -qxF "$1" "$WORK/umu.out" || die "umu-run: missing '$1'"; }
STEAM="$HOMEDIR/.local/share/Steam"
expect_line "arg $GAME/probe.exe"
expect_line "arg $GAME/Data"
expect_line "arg two words"
expect_line "env WINEPREFIX=$STEAM/steamapps/compatdata/489830"
expect_line "env GAMEID=umu-489830"
expect_line "env STORE=steam"
expect_line "env SteamAppId=489830"
expect_line "env SteamGameId=489830"
expect_line "env PROTONPATH=$STEAM/compatibilitytools.d/GE-Proton11-7"
expect_line "env STEAM_COMPAT_CLIENT_INSTALL_PATH=$STEAM"
expect_line "env MO2_E2E_VAR=hello"
expect_line "cwd $GAME"
```

seguido das mesmas verificações do primeiro cenário (probe passou, `overwrite/probe-created.txt`, `Data` real intacto, nada montado, helper terminou), extraídas para uma função `check_probe_run` usada pelos dois cenários. Atualizar o comentário do topo do `run.sh` com o cenário novo.

- [ ] **Step 4: Rodar**

Run (na distrobox): `./build.sh --tests`
Expected: `vfs e2e test passed`.

- [ ] **Step 5: Commit**

```bash
git add tests
git commit -m "tests: end-to-end Run of a Windows program through a fake umu-run"
```

---

### Task 13: Documentação, checklist manual e fechamento

**Files:**
- Create: `docs/testing/phase3-manual-checklist.md`
- Modify: `ROADMAP.md`, `STATE.md`, `README.md` (seção de uso, se citar a Fase 3), `docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md` (§3.2), `docs/superpowers/specs/2026-10-17-phase3-steam-proton-design.md` (§1.2 com o resultado da Task 3; status "aprovada; implementada" só depois do teste manual)

- [ ] **Step 1: Checklist manual**

`docs/testing/phase3-manual-checklist.md`, no formato do `phase2-manual-checklist.md` (pré-requisitos, passos numerados com o resultado esperado, o que coletar em caso de falha), cobrindo o §8 da spec:
0. Pré-requisitos: Bazzite com Steam aberta, Skyrim SE rodado ao menos uma vez pela Steam (cria o prefixo), SKSE64 instalado na pasta do jogo, um mod de teste com um `.esp` simples e um com um arquivo marcador, SSEEdit baixado; MO2 compilado na distrobox (`./build.sh`).
1. Nova instância Skyrim SE: detectada automaticamente; em Configurações → Caminhos e na aba de saves, "My Games" aponta para `.../compatdata/489830/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition`.
2. Executável `skse64_loader.exe` (adicionado ao lado do `SkyrimSE.exe`): "Executar" → janela do jogo → menu principal → o `.esp` de teste aparece em "Mods"/"Creations" ou o console responde a `getskseversion` → sair pelo menu → a interface destrava e `findmnt | grep -i skyrim` no host fica vazio.
3. SSEEdit como executável: abre, lista os plugins dos mods ativos; salvar um plugin de teste → o arquivo aparece em `overwrite/` (ou no mod escolhido em "Criar arquivos no mod"), não no `Data` real.
4. Proton por executável: escolher GE-Proton no grupo "Proton" do diálogo de executáveis e `DXVK_HUD=fps` em "Environment" → o contador de FPS do DXVK aparece; `logs/run-SkyrimSE.log` mostra a saída do GE-Proton.
5. Erros: `umu-run` ausente, simulado abrindo o MO2 com `MO2_VFS_BRIDGE=direct` dentro da distrobox (que não tem `umu-run`; no Bazzite o `/usr/bin` do host é imutável) → "Executar" do `SkyrimSE.exe` mostra "install umu-launcher on the host" e nada fica montado; renomear temporariamente `compatdata/489830` → "Executar" mostra "has no Proton prefix yet" e nada monta (desfazer o nome); escolher um Proton inexistente no executável editando o `ModOrganizer.ini` → mensagem com os Protons instalados.
6. Depois: `ls Data` real sem arquivos dos mods; `pfx/drive_c/users/` com `steamuser` e o link `<usuário> → steamuser` do `umu` (esperado, §1.1).
7. MO2 fechado durante o jogo: o jogo continua; ao sair dele, o mount some (`findmnt`).

- [ ] **Step 2: `STATE.md` e `ROADMAP.md`**

- `ROADMAP.md`: Fase 3 "🟡 Em teste manual" (vira "✅ Concluída (<data>)" só depois do teste do usuário), link para spec, plano e checklist; os riscos da seção Fase 3 trocados pelo resultado dos spikes (§1.2).
- `STATE.md`: "Limitações conhecidas da Fase 3" (jogo e ferramenta simultâneos no mesmo prefixo não suportados; programas que só existem no container não executam; o `umu` cria o link no prefixo; jogos sem prefixo precisam rodar uma vez pela Steam; cache de diretórios do Wine após `update` não testado); riscar da lista "Fase 3" de "Pendências por fase" os itens resolvidos (§9 da spec) e mover os que sobraram para "Fase 4" ou mantê-los com a nota "ficou para depois"; riscar da Fase 2 o item 6 (`Broken pipe`) e da Fase 4 o `println!` do `--cleanup`; acrescentar a pendência `/home` × `/var/home` como resolvida por construção; decisões novas em "Decisões tomadas" (VFS e programas no host; `spawn` no helper; `umu-run` obrigatório; espera do `wineserver`; `MO2_VFS_BRIDGE`).
- Spec geral §3.2: uma frase apontando para a spec da Fase 3 e as decisões que mudaram (host, `spawn`).
- Spec da Fase 3 §1.2: linha com o resultado da Task 3 (SKSE).

- [ ] **Step 3: Conferências finais**

Run: `./build.sh --tests` (na distrobox) e `cd mo2-vfs && cargo fmt --check && cargo clippy --all-targets -- -D warnings`
Expected: tudo verde.

- [ ] **Step 4: Commit e checkpoint de CI**

```bash
git add docs ROADMAP.md STATE.md README.md
git commit -m "docs: Phase 3 manual checklist, state and roadmap [windows-ci]"
```

O `[windows-ci]` no assunto dispara o CI Windows. Depois do push (com a autorização do usuário), conferir os dois CIs verdes. Em seguida: revisão final do branch, depois o teste manual pelo usuário com o checklist, e só então o merge em `main`.
