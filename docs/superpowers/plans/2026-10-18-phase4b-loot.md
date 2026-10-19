# Fase 4b — LOOT — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** "Ordenar" na aba de plugins do MO2 Linux roda o LOOT de verdade: um `lootcli` nativo (libloot 0.29.5 compilado do código-fonte), iniciado pelo `mo2-vfs` com o VFS montado sobre a pasta do jogo, baixa a masterlist, ordena os plugins dos mods e devolve relatório e ordem ao MO2.

**Architecture:** Três camadas. (1) Build: `build/libloot/build-libloot.sh` compila o wrapper C++ do libloot (Rust + `cxx`), e o alvo `lootcli` passa a compilar o executável, instalado em `bin/loot/` com o `libloot.so.0` ao lado (RUNPATH `$ORIGIN`), só com bibliotecas que o host Bazzite tem. (2) `deps/lootcli` ganha versão Linux em `#ifdef _WIN32`/`#else`: `main()`, pasta de dados do LOOT (Flatpak ou XDG) e download da masterlist com libcurl (`src/posix_support.*`). (3) O app inicia o lootcli por `VfsConnector::spawn()` (como o "Executar"), lê a saída de um arquivo nos logs da instância, espera o fim pelo `/proc` e o código pelo evento `exited`; o Cancelar usa um pedido `kill` novo do `mo2-vfs`.

**Tech Stack:** Rust (`mo2-vfs`, `rustix`); C++20/23, Qt 6 Core; libloot 0.29.5 (Rust + wrapper C++ por CMake/cargo); libcurl 8; toml++ 3.4 (só cabeçalhos); Boost.Locale 1.90; GoogleTest; Python 3 (driver do e2e); bash.

**Spec:** `docs/superpowers/specs/2026-10-18-phase4b-loot-design.md` (ler inteira antes de cada tarefa; §N são seções dela).

## Global Constraints

- Branch `fase-4b-loot` (já criado a partir de `main`; a spec está nele).
- Prefixos de commit: `deps/modorganizer: Linux: …` e `deps/lootcli: Linux: …` para mudanças nos subtrees (regra de `deps/README.md`); `mo2-vfs: …`, `build: …`, `tests: …`, `ci: …`, `docs: …` para o resto. Commits terminam com as linhas de atribuição da sessão; mensagens sem identificador de modelo.
- libloot: tag `0.29.5`, commit `a12fcfcef21b96ac6fcffaac06439d11e18db932`, de `https://github.com/loot/libloot`.
- O código Windows de `deps/lootcli` e de `deps/modorganizer` não muda: as versões Linux ficam em `#else` de `#ifdef _WIN32` ou em `src/linux/*_linux.cpp`. O app continua compilando no Windows contra o `uibase` upstream (CI Windows com `[windows-ci]`).
- Pasta de dados do LOOT: `<home>/.var/app/io.github.loot.loot/data/LOOT` se for diretório; senão `$XDG_DATA_HOME/LOOT` se `XDG_DATA_HOME` for absoluto e não vazio; senão `<home>/.local/share/LOOT`.
- Download da masterlist: libcurl, redirecionamentos, timeout total de 60 s (conexão 30 s), grava `<masterlist>.download` e só faz `rename` com sucesso (HTTP 200, ou `file://` lido). Erro: log `Error downloading masterlist: <motivo>` e código de saída 1.
- `kill` do `mo2-vfs`: `{"op":"kill","pid":N}`; só pids em `children`; outro pid → `error` com código `not_spawned`; `SIGTERM` ao grupo, `SIGKILL` após 3 s.
- Arquivos de uma execução do LOOT no Linux: `<dados da instância>/logs/lootcli.log`, `lootreport.json`, `loot-loadorder.txt` (`QDir::tempPath()` sem `dataPath`, nos testes).
- Bibliotecas que o executável `lootcli` pode exigir (`NEEDED`): `libloot.so.0 libQt6Core.so.6 libboost_locale.so.1.90.0 libcurl.so.4 libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6`.
- Mensagens ao usuário no código em inglês. Formatação: `clang-format` do subtree (`deps/modorganizer/.clang-format`, `deps/lootcli/.clang-format` se existir); Rust com `cargo fmt` e `cargo clippy` (sem avisos novos).
- Todo teste automático do app e os e2e definem `MO2_VFS_BRIDGE=direct` e não tocam o `~/.local/share/LOOT`, o Flatpak do LOOT nem o keyring reais.

## Ambiente de execução

O CI (GitHub Actions, container Fedora 44 de `build/Containerfile`) é a referência de "passou". No Bazzite, compile e teste dentro da distrobox `mo2-run`, a partir da raiz do superprojeto:

- Rust: `distrobox enter mo2-run -- bash -c 'export RUSTUP_HOME=$HOME/.cache/mo2-rustup CARGO_HOME=$HOME/.cache/mo2-cargo; cargo test --release --locked --manifest-path mo2-vfs/Cargo.toml --target-dir build-out/mo2-vfs'` (rustfmt/clippy também nesse `RUSTUP_HOME`);
- um alvo: `distrobox enter mo2-run -- ./build.sh --only <alvo> --tests`;
- só os testes do app, depois de um configure: `distrobox enter mo2-run -- bash -c 'cmake --build build-out/modorganizer --target organizer_tests && MO2_VFS_BINARY=$PWD/install/bin/mo2-vfs build-out/modorganizer/tests/organizer_tests --gtest_filter="<Filtro>*"'`.

Pacotes novos na distrobox (Task 3): `distrobox enter mo2-run -- sudo dnf -y install tomlplusplus-devel libcurl-devel`.

Se o MO2 do usuário estiver aberto, use um `TMPDIR` próprio nos testes (`TMPDIR=$PWD/build-out/tmp`). Nunca use `pkill -f`/`pgrep -f` com um padrão que aparece na própria linha de comando.

## Review Focus

1. **Arquivos de saída que ainda não existem, em pasta que é link simbólico** (`--out`, `--pluginListOutputPath`, o `output` do spawn): `hostCanonicalPath` devolve inalterado um caminho que não existe, e o host poderia não achar a pasta → `lootcliSpawnRequest` converte a pasta e acrescenta o nome; teste `LootcliSpawnRequestResolvesTheDirectoryOfNewFiles` (Task 7).
2. **Caminhos com espaços e não-ASCII** (`…/Módulos e Jogos/Skyrim Special Edition`): chegam ao lootcli como um argumento cada, sem aspas nem divisão → teste `LootcliSpawnRequestArguments` (Task 7) e o e2e roda numa pasta `Módulos e Jogos` (Task 5).
3. **Rede caída (conexão recusada) ou fonte inexistente na atualização da masterlist**: a masterlist anterior fica intacta e o erro aparece → testes `DownloadFailureKeepsTheOldFile` e `ConnectionErrorIsAnError` (Task 3); HTTP ≠ 200 fica no código (`status != 200`) e no teste manual.
4. **Biblioteca que o host não tem** (o lootcli roda no host pela distrobox): o build recusa um `NEEDED` fora da lista (Task 4), e quando o lootcli falha mesmo assim, as linhas do carregador dinâmico aparecem no diálogo → teste `LootcliOtherOutputKeepsTheLoaderError` (Task 7).
5. **Cancelar um lootcli que não sai com `SIGTERM`**, ou um pid que o helper não iniciou: `SIGKILL` em 3 s, e nenhum processo alheio é morto → testes `kill_of_a_program_that_ignores_sigterm_sends_sigkill` e `kill_of_a_pid_not_spawned_is_not_spawned` (Task 1) e `KillOfAnEndedProgramThrows` (Task 6).

---

### Task 1: `mo2-vfs` — pedido `kill`

**Files:**
- Modify: `mo2-vfs/src/session.rs` (`ErrorCode::NotSpawned`)
- Modify: `mo2-vfs/src/spawn.rs` (`signal_group`)
- Modify: `mo2-vfs/src/ipc.rs` (`Request::Kill`, `Server::kills`, `Server::poll_kills`, teste de parse)
- Modify: `mo2-vfs/src/bin/mo2-vfs.rs` (chama `poll_kills`)
- Test: `mo2-vfs/tests/protocol.rs`

**Interfaces:**
- Produces: pedido JSON `{"op":"kill","pid":<i32>}` → `{"event":"ok"}` ou `{"event":"error","code":"not_spawned",…}`; o `exited` do programa vem depois, como sempre. Rust: `Request::Kill { pid: i32 }`, `ErrorCode::NotSpawned`, `pub fn signal_group(pgid: i32, signal: rustix::process::Signal)` em `spawn.rs`, `pub fn poll_kills(&mut self)` em `Server`.

- [ ] **Step 1: Testes de protocolo que falham**

Em `mo2-vfs/tests/protocol.rs`, depois de `spawn_exit_by_signal_is_128_plus_signal`:

```rust
#[test]
fn kill_ends_a_spawned_program() {
    let env = Env::new();
    let mut c = Client::start(&env);
    let ev = c.request(spawn_request(&["sleep", "60"], &env.root));
    assert_eq!(ev["event"], "spawned", "{ev}");
    let pid = ev["pid"].as_i64().unwrap();
    let started = Instant::now();
    let ev = c.request(json!({"op": "kill", "pid": pid}));
    assert_eq!(ev, json!({"event": "ok"}));
    let (ev, _) = until(&mut c, "exited", |e| e["event"] == "exited");
    assert_eq!(ev, json!({"event": "exited", "pid": pid, "code": 143}));
    assert!(started.elapsed() < Duration::from_secs(5), "{:?}", started.elapsed());
    // reaped: no longer one of its programs
    let ev = c.request(json!({"op": "kill", "pid": pid}));
    assert_eq!(ev["event"], "error", "{ev}");
    assert_eq!(ev["code"], "not_spawned", "{ev}");
}

#[test]
fn kill_of_a_program_that_ignores_sigterm_sends_sigkill() {
    let env = Env::new();
    let mut c = Client::start(&env);
    // the ignored SIGTERM is inherited by sleep
    let ev = c.request(spawn_request(
        &["sh", "-c", "trap '' TERM; while :; do sleep 1; done"],
        &env.root,
    ));
    assert_eq!(ev["event"], "spawned", "{ev}");
    let pid = ev["pid"].as_i64().unwrap();
    // let the shell install its trap
    thread::sleep(Duration::from_millis(300));
    let started = Instant::now();
    assert_eq!(c.request(json!({"op": "kill", "pid": pid})), json!({"event": "ok"}));
    let (ev, _) = until(&mut c, "exited", |e| e["event"] == "exited");
    assert_eq!(ev, json!({"event": "exited", "pid": pid, "code": 137}));
    let took = started.elapsed();
    assert!(took >= Duration::from_secs(3), "SIGKILL before the grace period: {took:?}");
    assert!(took < Duration::from_secs(8), "{took:?}");
}

#[test]
fn kill_of_a_pid_not_spawned_is_not_spawned() {
    let env = Env::new();
    let mut c = Client::start(&env);
    let mut other = sleeper(30);
    let ev = c.request(json!({"op": "kill", "pid": other.id()}));
    assert_eq!(ev["event"], "error", "{ev}");
    assert_eq!(ev["code"], "not_spawned", "{ev}");
    thread::sleep(Duration::from_millis(200));
    assert!(other.try_wait().unwrap().is_none(), "a process it did not start was killed");
    other.kill().unwrap();
    other.wait().unwrap();
}
```

`EVENT_TIMEOUT` (10 s) já cobre a espera de ~3,5 s do segundo teste.

- [ ] **Step 2: Rodar e ver falhar**

Run: `distrobox enter mo2-run -- bash -c 'export RUSTUP_HOME=$HOME/.cache/mo2-rustup CARGO_HOME=$HOME/.cache/mo2-cargo; cargo test --release --locked --manifest-path mo2-vfs/Cargo.toml --target-dir build-out/mo2-vfs --test protocol kill_'`
Expected: os três FALHAM; a resposta ao `kill` é `{"event":"error","code":"bad_request","message":"invalid request: unknown variant `kill`…"}`.

- [ ] **Step 3: Código de erro e sinal ao grupo**

Em `mo2-vfs/src/session.rs`, no `enum ErrorCode`, depois de `SpawnFailed`:

```rust
    /// A program of a `spawn` request could not be started.
    SpawnFailed,
    /// The pid of a `kill` request is not a running program started by `spawn`.
    NotSpawned,
```

Em `mo2-vfs/src/spawn.rs`, depois de `start`:

```rust
/// Sends `signal` to the process group `pgid`; a group that is gone is not an error.
pub fn signal_group(pgid: i32, signal: rustix::process::Signal) {
    let Some(pid) = rustix::process::Pid::from_raw(pgid) else {
        return;
    };
    match rustix::process::kill_process_group(pid, signal) {
        Ok(()) | Err(rustix::io::Errno::SRCH) => {}
        Err(e) => tracing::warn!(pgid, error = %e, "cannot signal the process group"),
    }
}
```

- [ ] **Step 4: Pedido `kill` no servidor**

Em `mo2-vfs/src/ipc.rs`:

1. No `enum Request`, depois de `Spawn { … }`:

```rust
    Kill {
        pid: i32,
    },
```

2. Imports (o arquivo hoje não importa nada de `std::time`): `use std::time::{Duration, Instant};`, e a constante abaixo dos `use`:

```rust
/// How long a program gets to end after the SIGTERM of a `kill` request, before SIGKILL.
const KILL_GRACE: Duration = Duration::from_secs(3);
```

3. Em `struct Server`, depois de `children`:

```rust
    /// Groups of `kill` requests, with the time after which they get SIGKILL.
    kills: Vec<(i32, Instant)>,
```

4. Em `handle`, depois do braço `Request::Spawn { … } => { … }`:

```rust
            Request::Kill { pid } => {
                if !self.children.iter().any(|c| c.id() as i32 == pid) {
                    return Event::Error {
                        code: ErrorCode::NotSpawned,
                        message: format!("{pid} is not a running program started by spawn"),
                        path: None,
                        spontaneous: false,
                    };
                }
                tracing::info!(pid, "ending the program (SIGTERM to its group)");
                crate::spawn::signal_group(pid, rustix::process::Signal::TERM);
                if !self.kills.iter().any(|&(p, _)| p == pid) {
                    self.kills.push((pid, Instant::now() + KILL_GRACE));
                }
                Event::Ok
            }
```

5. Depois de `poll_children`:

```rust
    /// Sends SIGKILL to the groups of `kill` requests that are still alive after the grace
    /// period; forgets the ones that ended.
    pub fn poll_kills(&mut self) {
        let now = Instant::now();
        self.kills.retain(|&(pid, deadline)| {
            if !group_alive(pid) {
                return false;
            }
            if now < deadline {
                return true;
            }
            tracing::warn!(pid, "the program did not end after SIGTERM; sending SIGKILL");
            crate::spawn::signal_group(pid, rustix::process::Signal::KILL);
            false
        });
    }
```

6. No módulo de testes de `ipc.rs`, perto do teste de `spawn`, acrescente:

```rust
        assert!(matches!(
            parse_request(r#"{"op":"kill","pid":12}"#),
            Ok(Request::Kill { pid: 12 })
        ));
```

e acrescente `r#"{"op":"kill"}"#` à lista `for bad in [...]`.

Em `mo2-vfs/src/bin/mo2-vfs.rs`, no laço, logo depois do `for event in server.poll_children() { emit(&event); }`:

```rust
        server.poll_kills();
```

- [ ] **Step 5: Rodar todos os testes do `mo2-vfs`**

Run: `distrobox enter mo2-run -- bash -c 'export RUSTUP_HOME=$HOME/.cache/mo2-rustup CARGO_HOME=$HOME/.cache/mo2-cargo; cargo fmt --manifest-path mo2-vfs/Cargo.toml --check && cargo clippy --release --locked --manifest-path mo2-vfs/Cargo.toml --target-dir build-out/mo2-vfs --all-targets -- -D warnings && cargo test --release --locked --manifest-path mo2-vfs/Cargo.toml --target-dir build-out/mo2-vfs'`
Expected: fmt e clippy sem saída; todos os testes PASSAM (os de FUSE pulam sem `/dev/fuse`).

- [ ] **Step 6: Commit**

```bash
git add mo2-vfs/src mo2-vfs/tests/protocol.rs
git commit -m "mo2-vfs: kill request ends a spawned program (SIGTERM, then SIGKILL)"
```

---

### Task 2: Build do libloot 0.29.5

**Files:**
- Create: `build/libloot/build-libloot.sh`
- Modify: `build.sh` (alvo `libloot`)
- Modify: `.github/workflows/linux.yml` (cache)

**Interfaces:**
- Produces: `install/lib/libloot.so.0` (e o link `libloot.so`), `install/include/loot/*.h`, pacote CMake em `install/lib/cmake/libloot/` com o alvo importado `libloot::libloot`; alvo `libloot` do `build.sh`, antes de `lootcli`.

- [ ] **Step 1: Script**

`build/libloot/build-libloot.sh` (executável, `chmod 755`):

```bash
#!/usr/bin/env bash
# Build libloot 0.29.5, the C++ wrapper (cpp/ of the libloot repository) of the Rust
# library, as the shared library libloot.so and install it with its headers and CMake
# package into <root>/install. LOOT publishes binaries for Windows only (spec of
# Phase 4b, section 1.1); this is the version of the libloot port of the MO2 vcpkg
# registry, which the Windows build uses.
#
#   build/libloot/build-libloot.sh
#
# Environment (all optional):
#   INSTALL    install prefix            (default <root>/install)
#   BUILD_OUT  build tree root           (default <root>/build-out)
#
# Everything it fetches and builds stays in $BUILD_OUT/libloot (source, cargo home with
# the crates, build tree), which CI caches.
#
# Produces:
#   $INSTALL/lib/libloot.so.0 (and libloot.so)
#   $INSTALL/include/loot/**.h
#   $INSTALL/lib/cmake/libloot/   (find_package(libloot CONFIG), libloot::libloot)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
INSTALL="${INSTALL:-$ROOT/install}"
BUILD_OUT="${BUILD_OUT:-$ROOT/build-out}"

LIBLOOT_REPO="https://github.com/loot/libloot"
LIBLOOT_TAG="0.29.5"
# Commit the tag must resolve to (the tag is moveable, the commit is not).
LIBLOOT_COMMIT="a12fcfcef21b96ac6fcffaac06439d11e18db932"

WORK="$BUILD_OUT/libloot"
SRC="$WORK/src"
OBJ="$WORK/build"

log() { echo "==> libloot: $*"; }
die() { echo "build-libloot.sh: $*" >&2; exit 1; }

# --- fetch (cached in build-out/libloot/src, verified by commit hash) -------
head_of() { git -C "$SRC" rev-parse HEAD 2>/dev/null || true; }

if [[ $(head_of) != "$LIBLOOT_COMMIT" ]]; then
    log "fetching $LIBLOOT_REPO @ $LIBLOOT_TAG"
    rm -rf "$SRC" "$OBJ"
    mkdir -p "$WORK"
    git clone --quiet --depth 1 --branch "$LIBLOOT_TAG" -c advice.detachedHead=false \
        "$LIBLOOT_REPO" "$SRC"
    [[ $(head_of) == "$LIBLOOT_COMMIT" ]] ||
        die "tag $LIBLOOT_TAG resolved to $(head_of), expected $LIBLOOT_COMMIT"
fi
log "source at $LIBLOOT_COMMIT"

# --- build ------------------------------------------------------------------
# cargo (run by the CMake project of cpp/) keeps its crates here, so that a cached
# build-out/libloot needs no download; the revision is embedded in the library
export CARGO_HOME="$WORK/cargo"
export LIBLOOT_REVISION="${LIBLOOT_COMMIT:0:7}"
log "configure"
cmake -S "$SRC/cpp" -B "$OBJ" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX="$INSTALL" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DLIBLOOT_BUILD_TESTS=OFF \
    -DLIBLOOT_INSTALL_DOCS=OFF
log "build"
cmake --build "$OBJ"
log "install into $INSTALL"
cmake --install "$OBJ"

# --- check ------------------------------------------------------------------
lib="$(readlink -f "$INSTALL/lib/libloot.so")" || die "libloot.so was not installed"
soname="$(readelf -d "$lib" | sed -n 's/.*(SONAME).*\[\(.*\)\]/\1/p')"
[[ $soname == libloot.so.0 ]] || die "unexpected soname '$soname' (lootcli expects libloot.so.0, see build.sh)"
compgen -G "$INSTALL/lib/cmake/libloot/*onfig.cmake" >/dev/null ||
    die "the CMake package of libloot was not installed"
log "installed $lib ($soname)"
```

- [ ] **Step 2: Alvo no `build.sh`**

Em `ALL_TARGETS`, troque `esptk lootcli` por `esptk libloot lootcli`. Depois de `build_esptk() { … }`:

```bash
# libloot is LOOT's library (Rust, with a C++ wrapper), needed by the lootcli executable;
# the logic lives in build/libloot/build-libloot.sh (source pinned to 0.29.5).
build_libloot() {
    INSTALL="$INSTALL" BUILD_OUT="$BUILD_OUT" "$ROOT/build/libloot/build-libloot.sh"
}
```

- [ ] **Step 3: Compilar**

Run: `distrobox enter mo2-run -- ./build.sh --only libloot`
Expected: termina com `==> libloot: installed …/install/lib/libloot.so.0.29.5 (libloot.so.0)` (o nome do arquivo real pode variar; o soname tem de ser `libloot.so.0`). A primeira compilação leva vários minutos (Rust).

Se o soname não for `libloot.so.0`, **pare** e reporte: a Task 4 (lista `NEEDED`) depende dele.

- [ ] **Step 4: Cache no CI**

Em `.github/workflows/linux.yml`, entre "Build container image" e "Build and test":

```yaml
      # libloot (Rust, several minutes): its source, cargo home and build tree
      # (build/libloot/build-libloot.sh), keyed by the script (which pins the commit)
      # and the image
      - name: Cache libloot
        uses: actions/cache@v4
        with:
          path: ModOrganizerLinux/build-out/libloot
          key: libloot-${{ hashFiles('ModOrganizerLinux/build/libloot/build-libloot.sh', 'ModOrganizerLinux/build/Containerfile') }}
```

- [ ] **Step 5: Commit**

```bash
git add build/libloot/build-libloot.sh build.sh .github/workflows/linux.yml
git commit -m "build: libloot 0.29.5 from source (build/libloot), cached in CI"
```

---

### Task 3: lootcli — pasta de dados e download (`posix_support`) com testes

**Files:**
- Create: `deps/lootcli/src/posix_support.h`
- Create: `deps/lootcli/src/posix_support.cpp`
- Create: `deps/lootcli/tests/CMakeLists.txt`
- Create: `deps/lootcli/tests/test_posix_support.cpp`
- Modify: `deps/lootcli/src/CMakeLists.txt` (biblioteca `lootcli_posix`)
- Modify: `deps/lootcli/CMakeLists.txt` (testes)
- Modify: `build/Containerfile` (`tomlplusplus-devel libcurl-devel`)

**Interfaces:**
- Produces (namespace `lootcli::posix`, sem Qt):
  - `std::filesystem::path lootDataPath(const std::string& home, const std::string& xdgDataHome);`
  - `std::filesystem::path lootDataPath();` (de `HOME` e `XDG_DATA_HOME`)
  - `std::optional<std::string> downloadFile(const std::string& url, const std::filesystem::path& file);` (erro, ou `nullopt` no sucesso)
  - alvo CMake `lootcli_posix` (estático, só fora do Windows), ligado a `CURL::libcurl`.

- [ ] **Step 1: Pacotes**

Em `build/Containerfile`, na linha `libsecret-devel gnome-keyring dbus-daemon \`, acrescente ao fim (antes da barra) ` tomlplusplus-devel libcurl-devel`:

```dockerfile
        libsecret-devel gnome-keyring dbus-daemon tomlplusplus-devel libcurl-devel \
```

Na distrobox: `distrobox enter mo2-run -- sudo dnf -y install tomlplusplus-devel libcurl-devel`.

- [ ] **Step 2: Testes que falham**

`deps/lootcli/tests/test_posix_support.cpp`:

```cpp
#include "posix_support.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using lootcli::posix::downloadFile;
using lootcli::posix::lootDataPath;

namespace
{
// a temporary directory, removed on destruction
class TempDir
{
public:
  TempDir()
  {
    std::string tpl = (fs::temp_directory_path() / "lootcli-tests-XXXXXX").string();
    if (!::mkdtemp(tpl.data())) {
      throw std::runtime_error("mkdtemp failed");
    }
    m_path = tpl;
  }
  ~TempDir()
  {
    std::error_code ec;
    fs::remove_all(m_path, ec);
  }
  const fs::path& path() const { return m_path; }

private:
  fs::path m_path;
};

void write(const fs::path& p, const std::string& content)
{
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << content;
}

std::string read(const fs::path& p)
{
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string fileUrl(const fs::path& p)
{
  return "file://" + p.string();
}
}  // namespace

TEST(LootDataPath, FlatpakFolderWins)
{
  TempDir t;
  const auto flatpak = t.path() / ".var/app/io.github.loot.loot/data/LOOT";
  fs::create_directories(flatpak);
  EXPECT_EQ(lootDataPath(t.path().string(), "/xdg/data"), flatpak);
}

TEST(LootDataPath, XdgDataHome)
{
  TempDir t;
  EXPECT_EQ(lootDataPath(t.path().string(), "/xdg/data"), fs::path("/xdg/data/LOOT"));
}

TEST(LootDataPath, DefaultWithoutXdgDataHome)
{
  TempDir t;
  EXPECT_EQ(lootDataPath(t.path().string(), ""), t.path() / ".local/share/LOOT");
  // a relative XDG_DATA_HOME is invalid (XDG Base Directory specification)
  EXPECT_EQ(lootDataPath(t.path().string(), "relative"),
            t.path() / ".local/share/LOOT");
}

TEST(LootDataPath, FlatpakFileIsNotAFolder)
{
  TempDir t;
  write(t.path() / ".var/app/io.github.loot.loot/data/LOOT", "not a folder");
  EXPECT_EQ(lootDataPath(t.path().string(), ""), t.path() / ".local/share/LOOT");
}

TEST(LootDataPath, NoHome)
{
  EXPECT_EQ(lootDataPath("", ""), fs::path());
}

TEST(DownloadFile, ReplacesTheFile)
{
  TempDir t;
  write(t.path() / "source.yaml", "plugins: []\n");
  write(t.path() / "games/masterlist.yaml", "old\n");

  const auto error =
      downloadFile(fileUrl(t.path() / "source.yaml"), t.path() / "games/masterlist.yaml");

  EXPECT_FALSE(error) << *error;
  EXPECT_EQ(read(t.path() / "games/masterlist.yaml"), "plugins: []\n");
  EXPECT_FALSE(fs::exists(t.path() / "games/masterlist.yaml.download"));
}

TEST(DownloadFile, DownloadFailureKeepsTheOldFile)
{
  TempDir t;
  write(t.path() / "masterlist.yaml", "old\n");

  const auto error =
      downloadFile(fileUrl(t.path() / "missing.yaml"), t.path() / "masterlist.yaml");

  ASSERT_TRUE(error);
  EXPECT_FALSE(error->empty());
  EXPECT_EQ(read(t.path() / "masterlist.yaml"), "old\n");
  EXPECT_FALSE(fs::exists(t.path() / "masterlist.yaml.download"));
}

TEST(DownloadFile, ConnectionErrorIsAnError)
{
  // nothing listens on port 9 of localhost: a refused connection, without the network
  TempDir t;
  write(t.path() / "masterlist.yaml", "old\n");

  const auto error =
      downloadFile("http://127.0.0.1:9/masterlist.yaml", t.path() / "masterlist.yaml");

  ASSERT_TRUE(error);
  EXPECT_EQ(read(t.path() / "masterlist.yaml"), "old\n");
}

TEST(DownloadFile, UnwritableDestinationIsAnError)
{
  TempDir t;
  write(t.path() / "source.yaml", "x\n");

  const auto error = downloadFile(fileUrl(t.path() / "source.yaml"),
                                  t.path() / "no-such-dir/masterlist.yaml");

  ASSERT_TRUE(error);
  EXPECT_NE(error->find("no-such-dir"), std::string::npos) << *error;
}
```

`deps/lootcli/tests/CMakeLists.txt`:

```cmake
# Linux: tests of posix_support.cpp (the LOOT data folder and the masterlist download)
find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(lootcli_tests test_posix_support.cpp)
set_target_properties(lootcli_tests PROPERTIES CXX_STANDARD 20)
target_link_libraries(lootcli_tests PRIVATE lootcli_posix GTest::gtest_main)
gtest_discover_tests(lootcli_tests)
```

Em `deps/lootcli/CMakeLists.txt`, logo depois de `add_subdirectory(src)`:

```cmake
# the tests of the Linux parts (build.sh --tests sets BUILD_TESTING)
if (BUILD_TESTING AND NOT WIN32)
	enable_testing()
	add_subdirectory(tests)
endif()
```

Em `deps/lootcli/src/CMakeLists.txt`, logo depois de `cmake_minimum_required(VERSION 3.16)` (fora do `if (Qt6_FOUND)`, para os testes compilarem mesmo sem o executável):

```cmake
# Linux: the LOOT data folder and the masterlist download of lootcli (posix_support.cpp,
# the Windows versions are in lootthread.cpp), a library of their own for the tests
if (NOT WIN32)
	find_package(CURL REQUIRED)
	add_library(lootcli_posix STATIC posix_support.cpp posix_support.h)
	set_target_properties(lootcli_posix PROPERTIES CXX_STANDARD 20)
	target_include_directories(lootcli_posix PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
	target_link_libraries(lootcli_posix PUBLIC CURL::libcurl)
endif()
```

- [ ] **Step 3: Ver falhar**

Run: `distrobox enter mo2-run -- ./build.sh --only lootcli --tests`
Expected: FALHA na configuração ou na compilação: `posix_support.cpp`/`posix_support.h` não existem.

- [ ] **Step 4: Implementação**

`deps/lootcli/src/posix_support.h`:

```cpp
#ifndef LOOTCLI_POSIX_SUPPORT_H
#define LOOTCLI_POSIX_SUPPORT_H

// Linux parts of lootcli whose Windows versions are in lootthread.cpp
// (GetLOOTAppData(), LOOTWorker::GetFile()): the LOOT data folder and the download of
// the masterlist (spec of Phase 4b, section 3.3). No Qt, so that the tests need only
// this.

#include <filesystem>
#include <optional>
#include <string>

namespace lootcli::posix
{

// the LOOT data folder (settings.toml, games/<folder>/masterlist.yaml and
// userlist.yaml): the one of the Flatpak LOOT,
// <home>/.var/app/io.github.loot.loot/data/LOOT, if it is a folder, so that the rules
// the user made in LOOT apply, like %LOCALAPPDATA%\LOOT on Windows; else
// <xdgDataHome>/LOOT when xdgDataHome is absolute, else <home>/.local/share/LOOT;
// empty without a home
std::filesystem::path lootDataPath(const std::string& home,
                                   const std::string& xdgDataHome);

// lootDataPath() of HOME and XDG_DATA_HOME
std::filesystem::path lootDataPath();

// downloads `url` (http, https or file) into `file`: written to <file>.download first,
// which replaces `file` only when the download worked (HTTP 200, or a file that could be
// read), so that a failure keeps the old masterlist; follows redirections, gives up
// after 60 s; the error, or nullopt
std::optional<std::string> downloadFile(const std::string& url,
                                        const std::filesystem::path& file);

}  // namespace lootcli::posix

#endif  // LOOTCLI_POSIX_SUPPORT_H
```

`deps/lootcli/src/posix_support.cpp`:

```cpp
#include "posix_support.h"
#include "version.h"

#include <curl/curl.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace fs = std::filesystem;

namespace lootcli::posix
{

fs::path lootDataPath(const std::string& home, const std::string& xdgDataHome)
{
  if (home.empty()) {
    return {};
  }

  const auto flatpak = fs::path(home) / ".var/app/io.github.loot.loot/data/LOOT";
  std::error_code ec;
  if (fs::is_directory(flatpak, ec)) {
    return flatpak;
  }

  if (!xdgDataHome.empty() && fs::path(xdgDataHome).is_absolute()) {
    return fs::path(xdgDataHome) / "LOOT";
  }

  return fs::path(home) / ".local/share/LOOT";
}

fs::path lootDataPath()
{
  const char* home = std::getenv("HOME");
  const char* xdg  = std::getenv("XDG_DATA_HOME");
  return lootDataPath(home ? home : "", xdg ? xdg : "");
}

std::optional<std::string> downloadFile(const std::string& url, const fs::path& file)
{
  static const CURLcode initialised = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (initialised != CURLE_OK) {
    return std::string("curl_global_init failed: ") + curl_easy_strerror(initialised);
  }

  const fs::path partial = file.string() + ".download";
  FILE* out              = std::fopen(partial.c_str(), "wb");
  if (!out) {
    const auto e = errno;
    return "cannot open " + partial.string() + ": " + std::strerror(e);
  }

  std::optional<std::string> error;
  char message[CURL_ERROR_SIZE] = {};
  long status                   = 0;

  if (CURL* curl = curl_easy_init()) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);  // the default callback is fwrite()
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, message);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https,file");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "lootcli/" LOOTCLI_VERSION_STRING);

    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
      error = message[0] ? std::string(message) : std::string(curl_easy_strerror(rc));
    } else if (status != 0 && status != 200) {
      // 0: a file:// URL, which has no status
      error = "HTTP " + std::to_string(status);
    }
  } else {
    error = "curl_easy_init failed";
  }

  if (std::fclose(out) != 0 && !error) {
    const auto e = errno;
    error        = "cannot write " + partial.string() + ": " + std::strerror(e);
  }

  std::error_code ec;
  if (!error) {
    fs::rename(partial, file, ec);
    if (ec) {
      error = "cannot replace " + file.string() + ": " + ec.message();
    }
  }

  if (error) {
    fs::remove(partial, ec);
  }

  return error;
}

}  // namespace lootcli::posix
```

- [ ] **Step 5: Ver passar**

Run: `distrobox enter mo2-run -- ./build.sh --only lootcli --tests`
Expected: a etapa `test lootcli` mostra `100% tests passed` com os 9 testes de `LootDataPath`/`DownloadFile` (o executável ainda não compila: o `build.sh` continua desligando o Qt6 até a Task 4).

- [ ] **Step 6: Commits**

```bash
git add build/Containerfile
git commit -m "build: tomlplusplus-devel and libcurl-devel for lootcli (Phase 4b)"
git add deps/lootcli/src/posix_support.h deps/lootcli/src/posix_support.cpp \
    deps/lootcli/src/CMakeLists.txt deps/lootcli/CMakeLists.txt deps/lootcli/tests
git commit -m "deps/lootcli: Linux: LOOT data folder and masterlist download with libcurl"
```

---

### Task 4: lootcli — executável no Linux

**Files:**
- Modify: `deps/lootcli/src/main.cpp`
- Modify: `deps/lootcli/src/pch.h`
- Modify: `deps/lootcli/src/lootthread.h`
- Modify: `deps/lootcli/src/lootthread.cpp`
- Modify: `deps/lootcli/src/CMakeLists.txt`
- Modify: `build.sh` (`build_lootcli`)

**Interfaces:**
- Consumes: `lootcli::posix::lootDataPath()` e `lootcli::posix::downloadFile()` (Task 3); `libloot::libloot` (Task 2).
- Produces: `install/bin/loot/lootcli` (RUNPATH `$ORIGIN`) e `install/bin/loot/libloot.so.0`; mesmos parâmetros e mesmo protocolo de saída (`lootcli.h`) do Windows.

- [ ] **Step 1: O `build.sh` exige o executável (teste que falha)**

Troque `build_lootcli` e seu comentário por:

```bash
# lootcli: the executable (deps/lootcli on libloot, installed in bin/loot with the
# libloot.so.0 it needs, run path $ORIGIN) and the header package mo2-lootcli-header
# (mo2::lootcli-header). Mod Organizer starts it through mo2-vfs, which runs on the
# host when Mod Organizer runs in the mo2-run box (spec of Phase 4b, section 1.2): it
# must need nothing but libloot and libraries the host (Bazzite, Fedora 44) has,
# checked here against LOOTCLI_HOST_LIBS. With --tests, its tests run (ctest) and it
# must reject a missing argument.
LOOTCLI_HOST_LIBS="libloot.so.0 libQt6Core.so.6 libboost_locale.so.1.90.0 libcurl.so.4 libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6"
build_lootcli() {
    cmake_build lootcli "$ROOT/deps/lootcli"
    local exe="$INSTALL/bin/loot/lootcli" lib out
    [[ -x $exe ]] || die "lootcli was not installed in $INSTALL/bin/loot"
    [[ -e $INSTALL/bin/loot/libloot.so.0 ]] || die "libloot.so.0 is not next to lootcli"
    readelf -d "$exe" | grep -Eq '\((RUNPATH|RPATH)\).*\[\$ORIGIN\]' ||
        die "lootcli does not have the run path \$ORIGIN"
    for lib in $(readelf -d "$exe" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
        [[ " $LOOTCLI_HOST_LIBS " == *" $lib "* ]] ||
            die "lootcli needs $lib, which is not in LOOTCLI_HOST_LIBS (the host may not have it)"
    done
    if [[ $TESTS == 1 ]]; then
        out="$("$exe" --game SkyrimSE 2>&1)" && die "lootcli without --gamePath exited with 0"
        [[ $out == *"argument missing gamePath"* ]] || die "lootcli printed '$out'"
        log "lootcli: rejects a missing argument"
    fi
}
```

Run: `distrobox enter mo2-run -- ./build.sh --only lootcli --tests`
Expected: FALHA na compilação do executável (`Windows.h: No such file or directory` ou similar).

- [ ] **Step 2: `pch.h`**

Troque o bloco

```cpp
// windows
#define WIN32_LEAN_AND_MEAN
#include <Shlobj.h>
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <shellapi.h>
```

por

```cpp
// windows
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Shlobj.h>
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <shellapi.h>
#endif
```

- [ ] **Step 3: `main.cpp`**

Envolva a função `int wWinMain(...) { … }` inteira em `#ifdef _WIN32` … `#else` e acrescente, no `#else`:

```cpp
#else
#include <clocale>

// same as wWinMain(): argv is UTF-8 already
int main(int argc, char** argv)
{
  setlocale(LC_ALL, "C.UTF-8");

  const std::vector<std::string> arguments(argv, argv + argc);

  try {
    lootcli::LOOTWorker worker;

    worker.setUpdateMasterlist(!getParameter<bool>(arguments, "skipUpdateMasterlist"));
    worker.setGame(getParameter<std::string>(arguments, "game"));
    worker.setGamePath(getParameter<std::string>(arguments, "gamePath"));

    const auto pluginListPath = getParameter<std::string>(arguments, "pluginListPath");
    worker.setPluginListPath(pluginListPath);
    worker.setReportOutputPath(getParameter<std::string>(arguments, "out"));
    worker.setSortedPluginListOutputPath(getOptionalParameter<std::string>(
        arguments, "pluginListOutputPath", pluginListPath));
    worker.setLogLevel(getLogLevel(arguments));

    const auto lang = getOptionalParameter<std::string>(arguments, "language", "");
    if (!lang.empty()) {
      worker.setLanguageCode(lang);
    }

    return worker.run();
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
#endif
```

(Duplicado de propósito: a spec §3.3 mantém o Windows intocado. O `std::endl` só no Linux: a linha precisa terminar para o `lootcliOtherOutput` da Task 7.)

- [ ] **Step 4: `lootthread.h` e `lootthread.cpp`**

`lootthread.h`: troque `  DWORD GetFile(const WCHAR* szUrl, const CHAR* szFileName);` por

```cpp
#ifdef _WIN32
  DWORD GetFile(const WCHAR* szUrl, const CHAR* szFileName);
#endif
```

`lootthread.cpp`:

1. Topo: troque

```cpp
#pragma comment(lib, "winhttp.lib")

#include "lootthread.h"
#include "game_settings.h"
#include "version.h"
#include <codecvt>
#include <fstream>
#include <strsafe.h>
#include <windows.h>
#include <winhttp.h>
```

por

```cpp
#ifdef _WIN32
#pragma comment(lib, "winhttp.lib")
#endif

#include "lootthread.h"
#include "game_settings.h"
#include "version.h"
#include <codecvt>
#include <fstream>
#ifdef _WIN32
#include <strsafe.h>
#include <windows.h>
#include <winhttp.h>
#else
#include "posix_support.h"
#endif
```

2. `GetLOOTAppData()`: envolva o corpo atual em `#ifdef _WIN32` e acrescente o Linux:

```cpp
fs::path GetLOOTAppData()
{
#ifdef _WIN32
  TCHAR path[MAX_PATH];

  HRESULT res = ::SHGetFolderPath(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
                                  SHGFP_TYPE_CURRENT, path);

  if (res == S_OK) {
    return fs::path(path) / "LOOT";
  } else {
    return fs::path("");
  }
#else
  // the Flatpak LOOT's folder, or the XDG one (posix_support.h)
  return posix::lootDataPath();
#endif
}
```

3. Envolva a definição inteira de `DWORD LOOTWorker::GetFile(…) { … }` em `#ifdef _WIN32` … `#endif`.

4. Em `run()`, no teste da masterlist:

```cpp
    if (!fs::exists(masterlistPath())) {
      if (!m_UpdateMasterlist) {
        log(loot::LogLevel::error,
            "Masterlist not found at: " + masterlistPath().string());
#ifdef _WIN32
        return FALSE;
#else
        // FALSE is 0, success; nothing was sorted
        return 1;
#endif
      }
      fs::create_directories(masterlistPath().parent_path());
    }
```

5. Em `run()`, o bloco `if (m_UpdateMasterlist) { … }`: envolva o corpo atual em `#ifdef _WIN32` e acrescente o Linux, ficando:

```cpp
    if (m_UpdateMasterlist) {
      progress(Progress::UpdatingMasterlist);
#ifdef _WIN32
      … (as linhas atuais, do std::wstring_convert até o return FALSE do erro) …
#else
      log(loot::LogLevel::info, "Downloading latest masterlist file from " +
                                    m_GameSettings.MasterlistSource() + " to " +
                                    masterlistPath().string());
      if (const auto error =
              posix::downloadFile(m_GameSettings.MasterlistSource(), masterlistPath())) {
        log(loot::LogLevel::error, "Error downloading masterlist: " + *error);
        return 1;
      }
#endif
    }
```

(`progress(Progress::UpdatingMasterlist);` fica fora do `#ifdef`, igual nos dois.)

- [ ] **Step 5: `src/CMakeLists.txt` do lootcli**

No bloco `if (Qt6_FOUND)`:

1. Tire `version.rc` da lista de `target_sources(lootcli PRIVATE …)` e acrescente, logo depois do `target_sources`:

```cmake
if (WIN32)
	target_sources(lootcli PRIVATE version.rc)
endif()
```

2. Troque o `target_link_libraries(lootcli PRIVATE libloot::libloot Boost::headers Boost::locale tomlplusplus::tomlplusplus Qt6::Core)` por:

```cmake
if (WIN32)
	target_link_libraries(lootcli
	    PRIVATE libloot::libloot Boost::headers Boost::locale
		tomlplusplus::tomlplusplus Qt6::Core)
else()
	# lootcli may run on the host (started through mo2-vfs from the mo2-run box), which
	# has no libtomlplusplus: toml++ is used header-only; the MSVC pragmas of pch.h are
	# not for GCC; the libloot.so next to it is found through $ORIGIN
	target_include_directories(lootcli PRIVATE
		$<TARGET_PROPERTY:tomlplusplus::tomlplusplus,INTERFACE_INCLUDE_DIRECTORIES>)
	target_compile_definitions(lootcli PRIVATE TOML_HEADER_ONLY=1)
	target_compile_options(lootcli PRIVATE -Wno-unknown-pragmas)
	target_link_libraries(lootcli
		PRIVATE libloot::libloot Boost::headers Boost::locale Qt6::Core lootcli_posix)
	set_target_properties(lootcli PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()
```

3. Troque o `install(FILES $<TARGET_FILE:lootcli> $<TARGET_FILE:libloot::libloot> DESTINATION bin/loot)` por:

```cmake
if (WIN32)
install(FILES
	$<TARGET_FILE:lootcli>
	$<TARGET_FILE:libloot::libloot>
DESTINATION bin/loot)
else()
# install(TARGETS) sets the run path; libloot with its soname link (libloot.so.0)
install(TARGETS lootcli RUNTIME DESTINATION bin/loot)
install(IMPORTED_RUNTIME_ARTIFACTS libloot::libloot LIBRARY DESTINATION bin/loot)
endif()
```

- [ ] **Step 6: Compilar até passar**

Run: `distrobox enter mo2-run -- ./build.sh --only lootcli --tests`
Expected: compila, `100% tests passed`, `==> lootcli: rejects a missing argument`, nenhuma reclamação de `NEEDED`.

Se aparecerem outros erros de compilação em `lootthread.cpp`/`game_settings.cpp`, corrija só com `#ifdef _WIN32` em volta de API Windows (o código Windows não muda); erros contra a API do libloot 0.29.5 (assinaturas de `CreateGameHandle`, `LoadPlugins`, `SortPlugins`) significam que o subtree não casa com a versão: **pare** e reporte em vez de adaptar o lootcli. Se o `build.sh` reclamar de um `NEEDED` (ex.: `libtomlplusplus.so.3`, `libicu…`), descubra quem puxa (`readelf -d`, `ldd`) e resolva no CMake (só cabeçalhos/ligação estática) — não acrescente à lista uma biblioteca que o host não tem (`ls /run/host/usr/lib64/<lib>` dentro da distrobox mostra o que o host tem).

- [ ] **Step 7: Rodar no host de verdade**

Run (no host, fora da distrobox): `install/bin/loot/lootcli --game SkyrimSE; echo "exit $?"`
Expected: `Error: argument missing gamePath` e `exit 1` — prova que o carregador do host achou todas as bibliotecas.

- [ ] **Step 8: Commits**

```bash
git add deps/lootcli/src
git commit -m "deps/lootcli: Linux: build the executable (main, data folder, curl download, \$ORIGIN)"
git add build.sh
git commit -m "build: lootcli executable, checked against the libraries of the host"
```

---

### Task 5: `tests/loot-e2e` — lootcli pelo `mo2-vfs` montado

**Files:**
- Create: `tests/loot-e2e/run.sh`
- Create: `tests/loot-e2e/drive.py`
- Create: `tests/fixtures/loot-e2e/masterlist.yaml`
- Modify: `build.sh` (`loot_e2e`)

**Interfaces:**
- Consumes: `install/bin/mo2-vfs` com `kill` (Task 1), `install/bin/loot/lootcli` (Task 4).
- Produces: `tests/loot-e2e/run.sh <install prefix> <cache dir>` (0 = passou; pula sem FUSE salvo `MO2_REQUIRE_FUSE=1`); função `loot_e2e` do `build.sh`.

- [ ] **Step 1: Masterlist do teste**

`tests/fixtures/loot-e2e/masterlist.yaml`:

```yaml
# Masterlist of the LOOT end-to-end test (tests/loot-e2e): one rule, Blank.esp after
# Blank - Different.esp, the opposite of the order of the test's plugins.txt
plugins:
  - name: 'Blank.esp'
    after: [ 'Blank - Different.esp' ]
```

- [ ] **Step 2: Driver**

`tests/loot-e2e/drive.py`:

```python
#!/usr/bin/env python3
"""End-to-end test of LOOT on Linux (spec of Phase 4b, section 5).

    drive.py <install prefix> <testing-plugins SkyrimSE/Data> <work directory>

lootcli, started by mo2-vfs with the virtual file system mounted over the data
directory of a fake Skyrim Special Edition, sorts plugins that only the mods have:
- mounted, with the masterlist of tests/fixtures/loot-e2e: exit 0, the mods' plugins
  are in the sorted load order, which follows the masterlist rule, and the report is
  valid JSON;
- mounted, masterlist downloaded from a file:// source in settings.toml: the same, and
  the masterlist was written;
- not mounted: the mods' plugins are not in the sorted load order (proof that the
  virtual file system is what showed them);
- no masterlist with --skipUpdateMasterlist: exit 1, "Masterlist not found".
Everything stays in the work directory (HOME and XDG_DATA_HOME point inside it).
"""

import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

INSTALL, PLUGINS, WORK = (Path(a).resolve() for a in sys.argv[1:4])
VFS = INSTALL / "bin/mo2-vfs"
LOOTCLI = INSTALL / "bin/loot/lootcli"
FIXTURE = Path(__file__).resolve().parent.parent / "fixtures/loot-e2e"
TIMEOUT = float(os.environ.get("MO2_LOOT_E2E_TIMEOUT", "120"))

# non-ASCII and spaces on the way, like real instances
ROOT = WORK / "Módulos e Jogos"
GAME = ROOT / "Skyrim Special Edition"
DATA = GAME / "Data"
MODS = ROOT / "mods"
PROFILE = ROOT / "profile"
XDG = WORK / "xdg"
LOOT_GAME = XDG / "LOOT/games/Skyrim Special Edition"


def fail(message):
    print(f"loot e2e test FAILED: {message}", file=sys.stderr)
    sys.exit(1)


class Helper:
    """mo2-vfs and its line protocol; events that come while waiting for the answer to
    a request (exited, unmounted) are kept for until()."""

    def __init__(self, env):
        self.proc = subprocess.Popen(
            [str(VFS), "--log", str(WORK / "mo2-vfs.log"), "--log-level", "debug"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, env=env)
        self.events = queue.Queue()
        self.pending = []
        threading.Thread(target=self._read, daemon=True).start()
        hello = self._next(10)
        if hello.get("event") != "hello":
            fail(f"mo2-vfs said {hello}")

    def _read(self):
        for line in self.proc.stdout:
            self.events.put(json.loads(line))
        self.events.put(None)

    def _next(self, timeout):
        try:
            event = self.events.get(timeout=timeout)
        except queue.Empty:
            fail(f"no event from mo2-vfs within {timeout:.0f}s")
        if event is None:
            fail("mo2-vfs ended")
        return event

    def request(self, request):
        self.proc.stdin.write(json.dumps(request) + "\n")
        self.proc.stdin.flush()
        while True:
            event = self._next(30)
            if event.get("event") in ("exited", "unmounted"):
                self.pending.append(event)
                continue
            return event

    def until(self, predicate, timeout):
        for i, event in enumerate(self.pending):
            if predicate(event):
                return self.pending.pop(i)
        deadline = time.monotonic() + timeout
        while True:
            event = self._next(max(0.1, deadline - time.monotonic()))
            if predicate(event):
                return event
            self.pending.append(event)

    def close(self):
        self.proc.stdin.close()
        self.proc.wait(30)


def set_up():
    DATA.mkdir(parents=True)
    (GAME / "SkyrimSE.exe").write_bytes(b"")
    shutil.copy(PLUGINS / "Blank.esm", DATA / "Skyrim.esm")
    (MODS / "ModA").mkdir(parents=True)
    shutil.copy(PLUGINS / "Blank.esm", MODS / "ModA/Blank.esm")
    shutil.copy(PLUGINS / "Blank.esp", MODS / "ModA/Blank.esp")
    (MODS / "ModB").mkdir(parents=True)
    shutil.copy(PLUGINS / "Blank - Different.esp", MODS / "ModB/Blank - Different.esp")
    PROFILE.mkdir(parents=True)
    (PROFILE / "plugins.txt").write_text("*Blank.esm\n*Blank.esp\n*Blank - Different.esp\n")
    (PROFILE / "loadorder.txt").write_text(
        "Skyrim.esm\nBlank.esm\nBlank.esp\nBlank - Different.esp\n")
    LOOT_GAME.mkdir(parents=True)
    shutil.copy(FIXTURE / "masterlist.yaml", LOOT_GAME / "masterlist.yaml")


def mount(helper):
    def mapping(mod):
        return {"source": str(MODS / mod), "destination": str(DATA),
                "is_directory": True, "create_target": False}

    event = helper.request({"op": "mount", "mappings": [mapping("ModB"), mapping("ModA")]})
    if event.get("event") != "ready":
        fail(f"mount: {event}")
    if not (DATA / "Blank.esp").exists():
        fail("the mounted data directory does not show the plugins of the mods")


def run_lootcli(helper, name, skip_update=True, env=None):
    """Runs lootcli through mo2-vfs; its exit code, output and sorted load order."""
    out = WORK / name
    out.mkdir()
    argv = [str(LOOTCLI), "--game", "SkyrimSE", "--gamePath", str(GAME),
            "--pluginListPath", str(PROFILE / "loadorder.txt"), "--logLevel", "debug",
            "--out", str(out / "lootreport.json"),
            "--pluginListOutputPath", str(out / "loot-loadorder.txt"),
            "--language", "en"]
    if skip_update:
        argv.append("--skipUpdateMasterlist")
    event = helper.request({"op": "spawn", "argv": argv, "env": env or {},
                            "cwd": str(LOOTCLI.parent), "output": str(out / "lootcli.log")})
    if event.get("event") != "spawned":
        fail(f"spawn of lootcli: {event}")
    pid = event["pid"]
    exited = helper.until(lambda e: e.get("event") == "exited" and e.get("pid") == pid,
                          TIMEOUT)
    output = (out / "lootcli.log").read_text(errors="replace")
    order_file = out / "loot-loadorder.txt"
    order = []
    if order_file.exists():
        order = [line for line in order_file.read_text().splitlines()
                 if line and not line.startswith("#")]
    return exited["code"], output, order, out


def check_sorted(name, code, output, order, out):
    if code != 0:
        fail(f"{name}: lootcli exited with {code}:\n{output}")
    if "[error]" in output:
        fail(f"{name}: lootcli logged an error:\n{output}")
    if "[progress]" not in output:
        fail(f"{name}: no progress in the output of lootcli:\n{output}")
    for plugin in ("Skyrim.esm", "Blank.esm", "Blank.esp", "Blank - Different.esp"):
        if plugin not in order:
            fail(f"{name}: {plugin} is not in the sorted load order {order}")
    if order.index("Blank - Different.esp") > order.index("Blank.esp"):
        fail(f"{name}: the masterlist rule was not applied: {order}")
    report = json.loads((out / "lootreport.json").read_text())
    if not isinstance(report.get("plugins"), list) or "stats" not in report:
        fail(f"{name}: unexpected report {report}")


def main():
    set_up()
    env = dict(os.environ, HOME=str(WORK / "home"), XDG_DATA_HOME=str(XDG),
               MO2_VFS_STATE_DIR=str(WORK / "state"))
    (WORK / "home").mkdir()
    helper = Helper(env)

    print("--- mounted, cached masterlist")
    mount(helper)
    check_sorted("mounted", *run_lootcli(helper, "mounted"))
    # its group ended: mo2-vfs unmounts by itself
    helper.until(lambda e: e.get("event") == "unmounted", 30)

    print("--- mounted, masterlist downloaded from file://")
    source = WORK / "served-masterlist.yaml"
    shutil.copy(FIXTURE / "masterlist.yaml", source)
    (XDG / "LOOT/settings.toml").write_text(
        '[[games]]\ngameId = "SkyrimSE"\nfolder = "Skyrim Special Edition"\n'
        f'masterlistSource = "{source.as_uri()}"\n')
    (LOOT_GAME / "masterlist.yaml").unlink()
    mount(helper)
    check_sorted("download", *run_lootcli(helper, "download", skip_update=False))
    if (LOOT_GAME / "masterlist.yaml").read_text() != source.read_text():
        fail("the downloaded masterlist was not written")
    helper.until(lambda e: e.get("event") == "unmounted", 30)

    print("--- not mounted")
    code, output, order, _ = run_lootcli(helper, "unmounted")
    if code == 0 and "Blank.esp" in order:
        fail(f"without the virtual file system lootcli still saw Blank.esp: {order}")

    print("--- no masterlist")
    empty = WORK / "empty-xdg"
    empty.mkdir()
    code, output, _, _ = run_lootcli(helper, "no-masterlist", env={"XDG_DATA_HOME": str(empty)})
    if code != 1 or "Masterlist not found" not in output:
        fail(f"no masterlist: exit {code}, expected 1 and 'Masterlist not found':\n{output}")

    helper.close()
    print("loot e2e test passed")


main()
```

- [ ] **Step 3: Script**

`tests/loot-e2e/run.sh` (executável):

```bash
#!/usr/bin/env bash
# End-to-end test of LOOT on Linux: lootcli, started by mo2-vfs with the virtual file
# system mounted, sorts the plugins of the mods of a fake Skyrim SE (see drive.py).
#
#   tests/loot-e2e/run.sh <install prefix> <cache directory>
#
# The plugins come from testing-plugins 1.6.2 (the plugins of libloot's tests),
# downloaded once into the cache directory and checked by SHA-256. Everything the
# test writes stays in a temporary directory, removed at the end unless something is
# still mounted there. Without /dev/fuse or fusermount3 the test is skipped with a
# message, unless MO2_REQUIRE_FUSE=1 (set in CI), where it fails.
set -euo pipefail

INSTALL="$(cd "${1:?usage: $0 <install prefix> <cache directory>}" && pwd)"
CACHE="${2:?usage: $0 <install prefix> <cache directory>}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TP_VERSION=1.6.2
TP_SHA256=f6e5b55e2669993ab650ba470424b725d1fab71ace979134a77de3373bd55620

fail() { echo "loot e2e test FAILED: $*" >&2; exit 1; }

[[ -x $INSTALL/bin/mo2-vfs ]] || fail "$INSTALL/bin/mo2-vfs not found"
[[ -x $INSTALL/bin/loot/lootcli ]] || fail "$INSTALL/bin/loot/lootcli not found"

missing=""
[[ -e /dev/fuse ]] || missing="/dev/fuse"
command -v fusermount3 >/dev/null || missing="${missing:+$missing and }fusermount3"
if [[ -n $missing ]]; then
    [[ ${MO2_REQUIRE_FUSE:-} == 1 ]] && fail "$missing is missing and MO2_REQUIRE_FUSE=1"
    echo "loot e2e test SKIPPED: $missing is missing (set MO2_REQUIRE_FUSE=1 to fail instead)"
    exit 0
fi

# --- testing-plugins ---------------------------------------------------------------
mkdir -p "$CACHE"
plugins="$CACHE/testing-plugins-$TP_VERSION/SkyrimSE/Data"
if [[ ! -f $plugins/Blank.esp ]]; then
    archive="$CACHE/testing-plugins-$TP_VERSION.tar.gz"
    curl -sSfL -o "$archive" \
        "https://github.com/Ortham/testing-plugins/archive/refs/tags/$TP_VERSION.tar.gz"
    echo "$TP_SHA256  $archive" | sha256sum -c --quiet - ||
        fail "testing-plugins $TP_VERSION does not match its SHA-256"
    tar -C "$CACHE" -xzf "$archive"
fi

# --- run ---------------------------------------------------------------------------
WORK="$(mktemp -d "${TMPDIR:-/tmp}/mo2-loot-e2e.XXXXXX")"

mounts_under() {
    awk -v d="$1/" '{ if (index($5 "/", d) == 1) print $5 }' /proc/self/mountinfo
}

cleanup() {
    local rc=$? m
    while read -r m; do
        [[ -n $m ]] || continue
        echo "cleanup: unmounting $m" >&2
        if [[ $(id -u) == 0 ]]; then umount -l "$m" || true; else fusermount3 -uz "$m" || true; fi
    done <<< "$(mounts_under "$WORK")"
    if [[ -n $(mounts_under "$WORK") ]]; then
        echo "cleanup: still mounted under $WORK, not removing it" >&2
    else
        rm -rf "$WORK"
    fi
    exit "$rc"
}
trap cleanup EXIT

if ! python3 -I "$HERE/drive.py" "$INSTALL" "$plugins" "$WORK"; then
    echo "--- mo2-vfs.log" >&2
    cat "$WORK/mo2-vfs.log" >&2 2>/dev/null || true
    exit 1
fi
[[ -z $(mounts_under "$WORK") ]] || fail "still mounted under $WORK after the test"
```

(O `mountinfo` escapa espaços como `\040`; o caminho de `WORK` vem do `mktemp` e não tem espaços, então o prefixo casa. Os pontos de montagem abaixo dele, com espaços, saem escapados no `$5`; o `umount`/`fusermount3` desse caso só roda depois de uma falha.)

- [ ] **Step 4: Ligar no `build.sh`**

Depois de `vfs_e2e() { … }`:

```bash
# With --tests, after every target: the end-to-end test of LOOT (tests/loot-e2e/run.sh,
# see there): lootcli, started by mo2-vfs with the virtual file system mounted, sorts
# the plugins of the mods of a fake Skyrim SE. It needs mo2-vfs and lootcli: always in
# a full build, and with --only when libloot, lootcli or mo2-vfs was built and both are
# installed.
loot_e2e() {
    [[ $TESTS == 1 ]] || return 0
    if ((${#ONLY[@]})); then
        [[ " ${ONLY[*]} " == *" libloot "* || " ${ONLY[*]} " == *" lootcli "* ||
            " ${ONLY[*]} " == *" mo2-vfs "* ]] || return 0
        if [[ ! -x $INSTALL/bin/mo2-vfs || ! -x $INSTALL/bin/loot/lootcli ]]; then
            log "loot end-to-end test skipped: build mo2-vfs and lootcli first"
            return 0
        fi
    fi
    log "loot end-to-end test"
    "$ROOT/tests/loot-e2e/run.sh" "$INSTALL" "$BUILD_OUT/testing-plugins" ||
        die "loot end-to-end test failed"
    log "loot end-to-end test passed"
}
```

No fim do arquivo, depois do bloco que chama `vfs_e2e`:

```bash
if [[ " ${failed[*]} " == *" mo2-vfs "* || " ${failed[*]} " == *" libloot "* ||
    " ${failed[*]} " == *" lootcli "* ]]; then
    log "loot end-to-end test skipped: a target it needs failed"
else
    loot_e2e
fi
```

- [ ] **Step 5: Rodar**

Run: `distrobox enter mo2-run -- ./build.sh --only mo2-vfs --only lootcli --tests`
Expected: `==> loot end-to-end test passed`, com as quatro seções (`--- mounted, cached masterlist`, `--- mounted, masterlist downloaded from file://`, `--- not mounted`, `--- no masterlist`).

Se "not mounted" falhar porque o lootcli ainda viu `Blank.esp`, a montagem não estava sendo o caminho: confira se um VFS de outro teste ficou montado (`grep mo2-vfs /proc/self/mountinfo`). Se "mounted" falhar com mensagem do libloadorder sobre o `plugins.txt`/local path, use systematic-debugging antes de mudar o teste: a premissa da spec §3.4 (perfil como local path) é a que o app usa.

- [ ] **Step 6: Commit**

```bash
git add tests/loot-e2e tests/fixtures/loot-e2e build.sh
git commit -m "tests: loot-e2e: lootcli through the mounted mo2-vfs sorts the mods' plugins"
```

---

### Task 6: App — `VfsConnector::kill`

**Files:**
- Modify: `deps/modorganizer/src/vfsconnector.h`
- Modify: `deps/modorganizer/src/linux/vfsconnector_linux.cpp`
- Test: `deps/modorganizer/tests/test_vfsconnector.cpp`

**Interfaces:**
- Consumes: pedido `kill` do `mo2-vfs` (Task 1).
- Produces: `virtual void VfsConnector::kill(qint64 pid)` (padrão: nada); `LinuxVfsConnector::kill` lança `std::runtime_error` com a mensagem do helper (`not_spawned`) ou "mo2-vfs is not running". Chamado só na thread principal.

- [ ] **Step 1: Testes que falham**

Em `tests/test_vfsconnector.cpp`, depois de `SpawnFailureIsException`:

```cpp
TEST_F(VfsConnectorTest, KillEndsASpawnedProgram)
{
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  TempDir t;

  VfsSpawnRequest r;
  r.argv             = {"sleep", "60"};
  r.workingDirectory = q(t.path());
  const auto pid     = vfs->spawn(r);
  ASSERT_GT(pid, 0);

  vfs->kill(pid);

  const auto code = exitcodes::take(pid, std::chrono::seconds(5));
  ASSERT_TRUE(code);
  EXPECT_EQ(*code, 143);  // 128 + SIGTERM
}

TEST_F(VfsConnectorTest, KillOfAnEndedProgramThrows)
{
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  TempDir t;

  VfsSpawnRequest r;
  r.argv             = {"true"};
  r.workingDirectory = q(t.path());
  const auto pid     = vfs->spawn(r);
  ASSERT_TRUE(exitcodes::take(pid, std::chrono::seconds(5)));

  EXPECT_THROW(vfs->kill(pid), std::runtime_error);
}

TEST_F(VfsConnectorTest, KillWithoutHelperThrows)
{
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  EXPECT_THROW(vfs->kill(12345), std::runtime_error);
}
```

Run: `distrobox enter mo2-run -- bash -c 'cmake --build build-out/modorganizer --target organizer_tests'`
Expected: FALHA de compilação (`'class VfsConnector' has no member named 'kill'`). (Sem configure ainda: `./build.sh --only mo2-vfs` instala o helper novo e `cmake_configure` via `./build.sh --only modorganizer --tests` configura; os testes do conector usam `install/bin/mo2-vfs` com o `kill` da Task 1.)

- [ ] **Step 2: Interface**

Em `src/vfsconnector.h`, depois de `spawn()` (antes de `runningPrograms()`):

```cpp
  // ends a program started by spawn() and its process group: the helper sends SIGTERM,
  // then SIGKILL after 3 s (a program on the host may not be signalled from a
  // container); its exit code comes as usual; throws std::runtime_error with the reason
  // when the helper refuses (not one of its running programs) or is not running
  virtual void kill(qint64 pid) { Q_UNUSED(pid); }
```

Em `src/linux/vfsconnector_linux.cpp`, na classe `LinuxVfsConnector`, depois de `spawn()`:

```cpp
  void kill(qint64 pid) override
  {
    if (!m_client || !m_client->running()) {
      throw std::runtime_error("mo2-vfs is not running");
    }

    QJsonObject r;
    r["op"]  = "kill";
    r["pid"] = pid;

    log::debug("asking mo2-vfs to end {}", pid);
    send(r, 10s);
  }
```

- [ ] **Step 3: Ver passar**

Run: `distrobox enter mo2-run -- bash -c 'cmake --build build-out/modorganizer --target organizer_tests && MO2_VFS_BINARY=$PWD/install/bin/mo2-vfs build-out/modorganizer/tests/organizer_tests --gtest_filter="VfsConnectorTest.*"'`
Expected: todos os `VfsConnectorTest` PASSAM, inclusive os três novos.

- [ ] **Step 4: Commit**

```bash
git add deps/modorganizer/src/vfsconnector.h deps/modorganizer/src/linux/vfsconnector_linux.cpp \
    deps/modorganizer/tests/test_vfsconnector.cpp
git commit -m "deps/modorganizer: Linux: VfsConnector::kill asks mo2-vfs to end a program"
```

---

### Task 7: App — partes Linux do LOOT (`loot_linux`)

**Files:**
- Modify: `deps/modorganizer/src/linux/loot_linux.h`
- Modify: `deps/modorganizer/src/linux/loot_linux.cpp`
- Create: `deps/modorganizer/tests/test_loot.cpp`

**Interfaces:**
- Consumes: `vfsbridge::hostCanonicalPath(const QString& path, const QString& rootDir = "/")` (`vfsbridge_linux.h`); `VfsSpawnRequest` (`vfsconnector.h`); `lootcli::parseMessage` (`lootcli/lootcli.h`); `AppConfig::logPath()`.
- Produces (em `loot_linux.h`, biblioteca `organizer_launch`):
  - `QString lootcliPath(const QString& binDirectory = QCoreApplication::applicationDirPath());` (sem mudança)
  - `QString lootUnavailableReason(const QString& binDirectory = …);` — mensagem `lootcli was not found in <bin>/loot.`
  - `void disableIfLootUnavailable(QAbstractButton* button);` (sem mudança)
  - `QString lootFilesDirectory(const QString& dataPath = qApp->property("dataPath").toString());`
  - `struct LootcliRun { QString lootcli, game, gamePath, pluginListPath, logLevel, reportPath, sortedPluginListPath, outputPath, language; bool skipUpdateMasterlist = false; };`
  - `VfsSpawnRequest lootcliSpawnRequest(const LootcliRun& run, const QString& rootDir = "/");`
  - `QStringList lootcliOtherOutput(const QString& outputPath, int max = 20);`
  - `class AsyncPipe { explicit AsyncPipe(QString path = lootFilesDirectory() + "/lootcli.log"); env::HandlePtr create(); std::string read(); const QString& path() const; }`
  - O stub `Loot::waitForCompletion()` sai de `loot_linux.cpp` (vai para a Task 8).

- [ ] **Step 1: Testes que falham**

`deps/modorganizer/tests/test_loot.cpp`:

```cpp
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <filesystem>
#include <fstream>

#include "linux/loot_linux.h"
#include "tempdir.h"

namespace fs = std::filesystem;

namespace
{
QString q(const fs::path& p)
{
  return QString::fromStdString(p.string());
}

QString canonical(const fs::path& p)
{
  return QFileInfo(q(p)).canonicalFilePath();
}

void append(const fs::path& p, const std::string& s)
{
  std::ofstream(p, std::ios::binary | std::ios::app) << s;
}

// the tests run with MO2_VFS_BRIDGE=direct: hostCanonicalPath() is the canonical path
class LootLinuxTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    m_oldBridge = qgetenv("MO2_VFS_BRIDGE");
    qputenv("MO2_VFS_BRIDGE", "direct");
  }
  void TearDown() override
  {
    if (m_oldBridge.isNull()) {
      qunsetenv("MO2_VFS_BRIDGE");
    } else {
      qputenv("MO2_VFS_BRIDGE", m_oldBridge);
    }
  }

  LootcliRun run(const fs::path& root) const
  {
    LootcliRun r;
    r.lootcli              = q(root / "bin/loot/lootcli");
    r.game                 = "SkyrimSE";
    r.gamePath             = q(root / "Módulos e Jogos/Skyrim Special Edition");
    r.pluginListPath       = q(root / "instance/profiles/Default/loadorder.txt");
    r.logLevel             = "info";
    r.reportPath           = q(root / "instance/logs/lootreport.json");
    r.sortedPluginListPath = q(root / "instance/logs/loot-loadorder.txt");
    r.outputPath           = q(root / "instance/logs/lootcli.log");
    r.language             = "pt_BR";
    return r;
  }

  void makeTree(TempDir& t) const
  {
    t.createFile("bin/loot/lootcli");
    t.createFile("instance/profiles/Default/loadorder.txt");
    fs::create_directories(t.path() / "Módulos e Jogos/Skyrim Special Edition/Data");
    fs::create_directories(t.path() / "instance/logs");
  }

private:
  QByteArray m_oldBridge;
};
}  // namespace

TEST_F(LootLinuxTest, LootFilesDirectoryIsTheLogsOfTheInstance)
{
  TempDir t;
  const auto dir = lootFilesDirectory(q(t.path()));
  EXPECT_EQ(dir, q(t.path() / "logs"));
  EXPECT_TRUE(QFileInfo(dir).isDir());
}

TEST_F(LootLinuxTest, LootFilesDirectoryWithoutDataPathIsTemp)
{
  EXPECT_EQ(lootFilesDirectory(QString()), QDir::tempPath());
}

TEST_F(LootLinuxTest, LootcliSpawnRequestArguments)
{
  TempDir t;
  makeTree(t);
  const auto root = fs::path(canonical(t.path()).toStdString());

  const auto r = lootcliSpawnRequest(run(root));

  const QStringList expected = {
      q(root / "bin/loot/lootcli"),
      "--game",
      "SkyrimSE",
      "--gamePath",
      q(root / "Módulos e Jogos/Skyrim Special Edition"),
      "--pluginListPath",
      q(root / "instance/profiles/Default/loadorder.txt"),
      "--logLevel",
      "info",
      "--out",
      q(root / "instance/logs/lootreport.json"),
      "--pluginListOutputPath",
      q(root / "instance/logs/loot-loadorder.txt"),
      "--language",
      "pt_BR"};
  EXPECT_EQ(r.argv, expected);
  EXPECT_EQ(r.workingDirectory, q(root / "bin/loot"));
  EXPECT_EQ(r.output, q(root / "instance/logs/lootcli.log"));
  EXPECT_TRUE(r.winePrefix.isEmpty());
  EXPECT_TRUE(r.environment.isEmpty());
}

TEST_F(LootLinuxTest, LootcliSpawnRequestSkipsTheMasterlistUpdate)
{
  TempDir t;
  makeTree(t);
  auto r = run(t.path());
  r.skipUpdateMasterlist = true;

  EXPECT_EQ(lootcliSpawnRequest(r).argv.last(), "--skipUpdateMasterlist");
}

TEST_F(LootLinuxTest, LootcliSpawnRequestResolvesTheDirectoryOfNewFiles)
{
  // the logs are reached through a link; the output files don't exist yet
  TempDir t;
  makeTree(t);
  fs::create_symlink(t.path() / "instance/logs", t.path() / "logs-link");
  auto r       = run(t.path());
  r.outputPath = q(t.path() / "logs-link/lootcli.log");
  r.reportPath = q(t.path() / "logs-link/lootreport.json");

  const auto request = lootcliSpawnRequest(r);

  const auto logs = canonical(t.path() / "instance/logs");
  EXPECT_EQ(request.output, logs + "/lootcli.log");
  EXPECT_EQ(request.argv[request.argv.indexOf("--out") + 1], logs + "/lootreport.json");
}

TEST_F(LootLinuxTest, AsyncPipeReadsWhatIsAppended)
{
  TempDir t;
  const auto file = t.path() / "lootcli.log";
  append(file, "old output\n");

  AsyncPipe pipe(q(file));
  ASSERT_TRUE(pipe.create());
  EXPECT_EQ(fs::file_size(file), 0u);  // emptied
  EXPECT_EQ(pipe.read(), "");

  append(file, "[progress] 1\n[info] a");
  EXPECT_EQ(pipe.read(), "[progress] 1\n[info] a");
  append(file, "bc\n");
  EXPECT_EQ(pipe.read(), "bc\n");
  EXPECT_EQ(pipe.read(), "");
}

TEST_F(LootLinuxTest, AsyncPipeInMissingDirectoryFails)
{
  TempDir t;
  AsyncPipe pipe(q(t.path() / "missing/lootcli.log"));
  EXPECT_FALSE(pipe.create());
  EXPECT_EQ(pipe.read(), "");
}

TEST_F(LootLinuxTest, LootcliOtherOutputKeepsTheLoaderError)
{
  TempDir t;
  const auto file = t.path() / "lootcli.log";
  append(file, "[progress] 1\n[info] reading plugins\n"
               "lootcli: error while loading shared libraries: libfoo.so.1: cannot "
               "open shared object file: No such file or directory\n"
               "\n[error] sorting failed\nError: last line without newline");

  const auto lines = lootcliOtherOutput(q(file));

  ASSERT_EQ(lines.size(), 2);
  EXPECT_TRUE(lines[0].startsWith("lootcli: error while loading shared libraries"));
  EXPECT_EQ(lines[1], "Error: last line without newline");
}

TEST_F(LootLinuxTest, LootcliOtherOutputKeepsTheLastLines)
{
  TempDir t;
  const auto file = t.path() / "lootcli.log";
  for (int i = 0; i < 30; ++i) {
    append(file, "line " + std::to_string(i) + "\n");
  }

  const auto lines = lootcliOtherOutput(q(file), 20);

  ASSERT_EQ(lines.size(), 20);
  EXPECT_EQ(lines.front(), "line 10");
  EXPECT_EQ(lines.back(), "line 29");
  EXPECT_TRUE(lootcliOtherOutput(q(t.path() / "missing.log")).isEmpty());
}

TEST_F(LootLinuxTest, LootUnavailableReason)
{
  TempDir t;
  EXPECT_EQ(lootUnavailableReason(q(t.path())),
            QString("lootcli was not found in %1.").arg(q(t.path() / "loot")));

  t.createFile("loot/lootcli");
  EXPECT_FALSE(lootUnavailableReason(q(t.path())).isEmpty());  // not executable

  fs::permissions(t.path() / "loot/lootcli", fs::perms::owner_all);
  EXPECT_TRUE(lootUnavailableReason(q(t.path())).isEmpty());
}
```

(`TempDir::createFile(relative, size = 0)` é o de `tests/tempdir.h`; ele cria as pastas e um arquivo de `size` bytes.)

Run: `distrobox enter mo2-run -- ./build.sh --only modorganizer --tests`
Expected: FALHA de compilação em `test_loot.cpp` (`lootFilesDirectory`, `LootcliRun`… não declarados). O CMake dos testes pega todo `.cpp` da pasta (`mo2_configure_tests`), então não há lista a editar.

- [ ] **Step 2: `loot_linux.h`**

Substitua o arquivo por:

```cpp
#ifndef MODORGANIZER_LOOT_LINUX_H
#define MODORGANIZER_LOOT_LINUX_H

// Linux parts of loot.cpp (src/linux/loot_linux.cpp; the members of Loot are in
// src/linux/lootrun_linux.cpp): where lootcli is, whether LOOT can be used, where the
// files of a run go, how mo2-vfs starts lootcli and the file that receives its output
// (spec of Phase 4b, section 3.4).

#include "../envmodule.h"
#include "../vfsconnector.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <string>

class QAbstractButton;

// the lootcli executable, <bin>/loot/lootcli
//
QString
lootcliPath(const QString& binDirectory = QCoreApplication::applicationDirPath());

// why LOOT can't be used, or an empty string if it can: it can when an executable
// lootcli is in the loot directory
//
QString lootUnavailableReason(
    const QString& binDirectory = QCoreApplication::applicationDirPath());

// disables the given button, with the reason as its tooltip, if LOOT can't be used
//
void disableIfLootUnavailable(QAbstractButton* button);

// the directory of the files of a LOOT run (the output of lootcli, its report and the
// sorted load order), created if needed: the logs of the instance, <dataPath>/logs,
// which the host sees too when Mod Organizer runs in a container (whose /tmp may not be
// the host's); the temporary directory without a data path (the tests)
//
QString
lootFilesDirectory(const QString& dataPath = qApp->property("dataPath").toString());

// a run of lootcli
//
struct LootcliRun
{
  QString lootcli;               // the executable, lootcliPath()
  QString game;                  // IPluginGame::lootGameName()
  QString gamePath;              // the game directory, where mo2-vfs mounts
  QString pluginListPath;        // <profile>/loadorder.txt
  QString logLevel;              // lootcli::logLevelToString()
  QString reportPath;            // lootreport.json
  QString sortedPluginListPath;  // the sorted load order
  QString outputPath;            // the output of lootcli, read by AsyncPipe
  QString language;
  bool skipUpdateMasterlist = false;
};

// how mo2-vfs starts lootcli: the arguments of the Windows command line as a list
// (without its quotes), every path as the host sees it (vfsbridge::hostCanonicalPath(),
// the directory of a file, which may not exist yet), the loot directory as working
// directory, the output to outputPath, no Wine prefix
//
VfsSpawnRequest lootcliSpawnRequest(const LootcliRun& run, const QString& rootDir = "/");

// the last `max` lines of the output of lootcli that are not messages of its protocol
// (lootcli/lootcli.h): what the dynamic loader or a crash wrote; shown when it fails
//
QStringList lootcliOtherOutput(const QString& outputPath, int max = 20);

// receives the output of lootcli; on Windows, a named pipe in loot.cpp; here the file
// mo2-vfs writes it to (lootcli runs where mo2-vfs runs, maybe on the host: there is no
// pipe between them)
//
class AsyncPipe
{
public:
  explicit AsyncPipe(QString path = lootFilesDirectory() + "/lootcli.log");
  ~AsyncPipe();

  AsyncPipe(const AsyncPipe&)            = delete;
  AsyncPipe& operator=(const AsyncPipe&) = delete;

  // creates (or empties) the file and opens it for reading; the HANDLE is the file
  // descriptor, which this pipe owns (env::HandlePtr doesn't close anything on Linux);
  // empty on failure
  //
  env::HandlePtr create();

  // what has been written to the file since the last call, without blocking
  //
  std::string read();

  // the file
  //
  const QString& path() const { return m_path; }

private:
  QString m_path;
  int m_fd = -1;
};

#endif  // MODORGANIZER_LOOT_LINUX_H
```

- [ ] **Step 3: `loot_linux.cpp`**

Substitua o arquivo por:

```cpp
// Linux implementations of the Win32-only parts of loot.cpp that don't need the rest of
// the application (loot_linux.h); the members of Loot that start lootcli and wait for
// it are in lootrun_linux.cpp.

#include "loot_linux.h"
#include "../shared/appconfig.h"
#include "vfsbridge_linux.h"

#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>

#include <lootcli/lootcli.h>
#include <uibase/log.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string_view>
#include <unistd.h>
#include <utility>

using namespace MOBase;

namespace
{
// a file as the host sees it: its directory through hostCanonicalPath() (which returns
// the path of a file that doesn't exist yet unchanged) and its name
QString hostFilePath(const QString& path, const QString& rootDir)
{
  const QFileInfo info(path);
  return vfsbridge::hostCanonicalPath(info.absolutePath(), rootDir) + "/" +
         info.fileName();
}
}  // namespace

QString lootcliPath(const QString& binDirectory)
{
  return binDirectory + "/loot/lootcli";
}

QString lootUnavailableReason(const QString& binDirectory)
{
  const QFileInfo lootcli(lootcliPath(binDirectory));

  if (lootcli.isFile() && lootcli.isExecutable()) {
    return {};
  }

  return QObject::tr("lootcli was not found in %1.").arg(binDirectory + "/loot");
}

void disableIfLootUnavailable(QAbstractButton* button)
{
  const auto reason = lootUnavailableReason();

  if (!reason.isEmpty()) {
    button->setDisabled(true);
    button->setToolTip(reason);
  }
}

QString lootFilesDirectory(const QString& dataPath)
{
  if (dataPath.isEmpty()) {
    return QDir::tempPath();
  }

  const auto dir = dataPath + "/" + QString::fromStdWString(AppConfig::logPath());
  QDir().mkpath(dir);
  return dir;
}

VfsSpawnRequest lootcliSpawnRequest(const LootcliRun& run, const QString& rootDir)
{
  VfsSpawnRequest r;

  r.argv = {hostFilePath(run.lootcli, rootDir),
            "--game",
            run.game,
            "--gamePath",
            vfsbridge::hostCanonicalPath(run.gamePath, rootDir),
            "--pluginListPath",
            hostFilePath(run.pluginListPath, rootDir),
            "--logLevel",
            run.logLevel,
            "--out",
            hostFilePath(run.reportPath, rootDir),
            "--pluginListOutputPath",
            hostFilePath(run.sortedPluginListPath, rootDir),
            "--language",
            run.language};

  if (run.skipUpdateMasterlist) {
    r.argv << "--skipUpdateMasterlist";
  }

  r.workingDirectory =
      vfsbridge::hostCanonicalPath(QFileInfo(run.lootcli).absolutePath(), rootDir);
  r.output = hostFilePath(run.outputPath, rootDir);

  return r;
}

QStringList lootcliOtherOutput(const QString& outputPath, int max)
{
  QFile file(outputPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }

  QStringList lines;
  for (const auto& raw : file.readAll().split('\n')) {
    const auto line = raw.trimmed();
    if (line.isEmpty()) {
      continue;
    }

    const std::string_view view(line.constData(), static_cast<std::size_t>(line.size()));
    if (lootcli::parseMessage(view).type != lootcli::MessageType::None) {
      continue;
    }

    lines << QString::fromUtf8(line);
  }

  if (lines.size() > max) {
    lines = lines.mid(lines.size() - max);
  }

  return lines;
}

AsyncPipe::AsyncPipe(QString path) : m_path(std::move(path)) {}

AsyncPipe::~AsyncPipe()
{
  if (m_fd != -1) {
    ::close(m_fd);
  }
}

env::HandlePtr AsyncPipe::create()
{
  const auto native = QFile::encodeName(m_path);
  const int fd = ::open(native.constData(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

  if (fd == -1) {
    const auto e = errno;
    log::error("cannot create '{}' for the output of lootcli, {}", m_path,
               std::strerror(e));
    return {};
  }

  if (m_fd != -1) {
    ::close(m_fd);
  }
  m_fd = fd;

  return env::HandlePtr(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(m_fd)));
}

std::string AsyncPipe::read()
{
  if (m_fd == -1) {
    return {};
  }

  std::string out;
  char buffer[50000];

  for (;;) {
    const auto n = ::read(m_fd, buffer, sizeof(buffer));
    if (n <= 0) {
      // the end of what was written so far, or an error
      break;
    }
    out.append(buffer, static_cast<std::size_t>(n));
  }

  return out;
}
```

(O stub antigo de `Loot::waitForCompletion()` e o `#include "../loot.h"` saem daqui: a implementação real vem na Task 8, em `lootrun_linux.cpp`, que é do executável e não da `organizer_launch`. Até a Task 8 o app **não linka** — `Loot::waitForCompletion` fica sem definição. Por isso, nesta tarefa compile e rode só `organizer_tests`, e faça a Task 8 logo em seguida.)

- [ ] **Step 4: Ver passar**

Run: `distrobox enter mo2-run -- bash -c 'cmake --build build-out/modorganizer --target organizer_tests && MO2_VFS_BINARY=$PWD/install/bin/mo2-vfs build-out/modorganizer/tests/organizer_tests --gtest_filter="LootLinuxTest.*"'`
Expected: os 10 `LootLinuxTest` PASSAM.

Em seguida, a suíte toda: `distrobox enter mo2-run -- bash -c 'MO2_VFS_BINARY=$PWD/install/bin/mo2-vfs build-out/modorganizer/tests/organizer_tests'` → tudo PASSA.

- [ ] **Step 5: Commit**

```bash
git add deps/modorganizer/src/linux/loot_linux.h deps/modorganizer/src/linux/loot_linux.cpp \
    deps/modorganizer/tests/test_loot.cpp
git commit -m "deps/modorganizer: Linux: lootcli spawn request, output file and run directory"
```

---

### Task 8: App — o LOOT roda pelo `mo2-vfs`

**Files:**
- Create: `deps/modorganizer/src/linux/lootrun_linux.cpp`
- Modify: `deps/modorganizer/src/loot.cpp`

**Interfaces:**
- Consumes: tudo de `loot_linux.h` (Task 7); `VfsConnector::activate/spawn/kill/releaseIfIdle` (Task 6); `exitcodes::take(qint64, std::chrono::milliseconds)` (`exitcodes_linux.h`); `OrganizerCore::vfs()`, `managedGame()`, `profilePath()`, `settings()`.
- Produces: `Loot::spawnLootcli(QWidget*, bool, env::HandlePtr)` e `Loot::waitForCompletion()` no Linux. O resto de `Loot` não muda.

- [ ] **Step 1: `loot.cpp`**

1. Troque

```cpp
static QString LootReportPath       = QDir::temp().absoluteFilePath("lootreport.json");
static QString SortedPluginListPath = QDir::temp().absoluteFilePath("loadorder.txt");
```

por

```cpp
#ifdef _WIN32
static QString LootReportPath       = QDir::temp().absoluteFilePath("lootreport.json");
static QString SortedPluginListPath = QDir::temp().absoluteFilePath("loadorder.txt");
#else
// in the logs of the instance (lootFilesDirectory(), loot_linux.h), set by the
// constructor of Loot: the data path is not known yet when these are initialised
static QString LootReportPath;
static QString SortedPluginListPath;
#endif
```

2. Construtor: troque

```cpp
Loot::Loot(OrganizerCore& core)
    : m_core(core), m_thread(nullptr), m_cancel(false), m_result(false)
{}
```

por

```cpp
Loot::Loot(OrganizerCore& core)
    : m_core(core), m_thread(nullptr), m_cancel(false), m_result(false)
{
#ifndef _WIN32
  LootReportPath       = lootFilesDirectory() + "/lootreport.json";
  SortedPluginListPath = lootFilesDirectory() + "/loot-loadorder.txt";
#endif
}
```

3. `Loot::spawnLootcli`: envolva a definição inteira em `#ifdef _WIN32` … `#endif  // _WIN32` e, dentro dela, apague o bloco

```cpp
#ifndef _WIN32
  // not an .exe on Linux
  sp.binary = QFileInfo(lootcliPath());
#endif
```

(a versão Linux fica em `lootrun_linux.cpp`). `waitForCompletion` já está em `#ifdef _WIN32`.

- [ ] **Step 2: `lootrun_linux.cpp`**

`deps/modorganizer/src/linux/lootrun_linux.cpp`:

```cpp
// Linux implementations of the members of Loot (loot.h) that start lootcli and wait for
// it; the Windows ones are in loot.cpp, the free functions they use in loot_linux.cpp.
//
// Like Run (processrunner_linux.cpp), lootcli is started through mo2-vfs
// (VfsConnector::spawn()) with the virtual file system mounted over the game
// directory, the usvfs of Windows (spec of Phase 4b, section 3.4): it sees the plugins
// of the mods in the data directory. When Mod Organizer runs in a container, mo2-vfs
// and lootcli run on the host: the output of lootcli goes to a file in the logs of the
// instance (AsyncPipe reads it), the end of lootcli is seen through /proc (the PID
// namespace is the host's) and its exit code comes from the exited event of mo2-vfs
// (exitcodes_linux.h). Cancelling asks mo2-vfs to end it (VfsConnector::kill()): a
// process of the host may not be signalled from the container. When its process group
// ends, mo2-vfs unmounts unless another program uses the mount.

#include "../loot.h"
#include "../organizercore.h"
#include "../settings.h"
#include "exitcodes_linux.h"
#include "loot_linux.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QThread>

#include <uibase/iplugingame.h>
#include <uibase/log.h>
#include <uibase/report.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <sys/types.h>

using namespace MOBase;

namespace
{
// how long a cancelled lootcli may take to end (mo2-vfs sends SIGKILL after 3 s)
constexpr qint64 CancelTimeoutMs = 10'000;

pid_t pidOf(const env::HandlePtr& h)
{
  return static_cast<pid_t>(reinterpret_cast<std::intptr_t>(h.get()));
}

bool alive(pid_t pid)
{
  // EPERM: a process of the host that the container may not signal still runs
  return ::kill(pid, 0) == 0 || errno == EPERM;
}
}  // namespace

bool Loot::spawnLootcli(QWidget*, bool didUpdateMasterList, env::HandlePtr)
{
  LootcliRun run;
  run.lootcli        = lootcliPath();
  run.game           = m_core.managedGame()->lootGameName();
  run.gamePath       = m_core.managedGame()->gameDirectory().absolutePath();
  run.pluginListPath = m_core.profilePath() + "/loadorder.txt";
  run.logLevel       = QString::fromStdString(
      lootcli::logLevelToString(m_core.settings().diagnostics().lootLogLevel()));
  run.reportPath           = reportPath();
  run.sortedPluginListPath = sortedPluginListPath();
  run.outputPath           = m_pipe->path();
  run.language             = m_core.settings().interface().language();
  run.skipUpdateMasterlist = didUpdateMasterList;

  try {
    // Loot::start() gave the mappings to the file system (prepareVFS())
    m_core.vfs().activate();
    const auto pid = m_core.vfs().spawn(lootcliSpawnRequest(run));
    log::debug("lootcli started through mo2-vfs, pid {}", pid);
    m_lootProcess.reset(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(pid)));
    return true;
  } catch (const std::exception& e) {
    // nothing will end a group that keeps the file system mounted
    m_core.vfs().releaseIfIdle();
    log::error("failed to start lootcli: {}", e.what());
    // the dialog of LOOT opens only once lootcli runs
    reportError(tr("failed to start loot: %1").arg(QString::fromStdString(e.what())));
    return false;
  }
}

bool Loot::waitForCompletion()
{
  const auto pid     = pidOf(m_lootProcess);
  bool killRequested = false;
  QElapsedTimer sinceKill;

  log::debug("loot thread waiting for completion on lootcli ({})", pid);

  while (alive(pid)) {
    if (m_cancel && !killRequested) {
      log::debug("asking mo2-vfs to end lootcli");
      killRequested = true;
      sinceKill.start();

      // the connector belongs to the main thread; OrganizerCore outlives this run
      VfsConnector* vfs = &m_core.vfs();
      QMetaObject::invokeMethod(
          qApp,
          [vfs, pid] {
            try {
              vfs->kill(pid);
            } catch (const std::exception& e) {
              log::error("cannot end lootcli: {}", e.what());
            }
          },
          Qt::QueuedConnection);
    }

    if (killRequested && sinceKill.elapsed() > CancelTimeoutMs) {
      log::error("lootcli did not end within {} ms of the cancel", CancelTimeoutMs);
      return false;
    }

    processStdout(m_pipe->read());
    QThread::msleep(100);
  }

  processStdout(m_pipe->read());

  // taken even when cancelled, so that it doesn't stay recorded
  const auto code = exitcodes::take(pid, std::chrono::seconds(2));

  if (m_cancel) {
    log::debug("lootcli ended after the cancel");
    return false;
  }

  if (!code) {
    emit log(log::Levels::Error,
             tr("lootcli ended, but mo2-vfs did not report its exit code"));
    return false;
  }

  if (*code != 0) {
    // what the dynamic loader or a crash wrote, which is not in the protocol
    for (const auto& line : lootcliOtherOutput(m_pipe->path())) {
      m_errors.push_back(line);
      emit log(log::Levels::Error, line);
    }

    emit log(log::Levels::Error, tr("Loot failed. Exit code was: 0x%1").arg(*code, 0, 16));
    return false;
  }

  return true;
}
```

O `mo2_configure_target` acrescenta todo `.cpp` de `src/` (inclusive `src/linux/`) ao executável `organizer`; `lootrun_linux.cpp` não está nas listas das bibliotecas (`_organizer_launch_sources` etc.), então vai para o executável sem mudar o CMake. Confira com `grep lootrun build-out/modorganizer/build.ninja` depois do configure.

- [ ] **Step 3: Compilar o app, testes e smoke**

Run: `distrobox enter mo2-run -- ./build.sh --only modorganizer --tests`
Expected: `organizer_tests` todos PASSAM, o app compila e linka, `smoke test passed`.

- [ ] **Step 4: Build completo com todos os testes**

Run: `distrobox enter mo2-run -- ./build.sh --tests`
Expected: termina sem erro, com `vfs end-to-end test passed` e `loot end-to-end test passed`.

- [ ] **Step 5: Conferência rápida na interface (opcional, com o usuário se ele quiser)**

Abra o MO2 do `install/` pela distrobox numa instância de Skyrim SE e clique em "Ordenar": o diálogo do LOOT aparece com progresso e termina; `<instância>/logs/lootcli.log` tem as linhas `[progress]`. Isso não substitui a Task 10.

- [ ] **Step 6: Commit**

```bash
git add deps/modorganizer/src/loot.cpp deps/modorganizer/src/linux/lootrun_linux.cpp
git commit -m "deps/modorganizer: Linux: LOOT runs lootcli through mo2-vfs"
```

---

### Task 9: Documentação, checklist manual e ponto de controle de CI

**Files:**
- Create: `docs/testing/phase4b-manual-checklist.md`
- Modify: `README.md`, `STATE.md`, `ROADMAP.md`

- [ ] **Step 1: Checklist manual**

`docs/testing/phase4b-manual-checklist.md`:

```markdown
# Fase 4b — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-18-phase4b-loot-design.md`](../superpowers/specs/2026-10-18-phase4b-loot-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 compilado na distrobox `mo2-run` (`./build.sh`, resultado em `install/`) e iniciado **pelo box**: `distrobox enter mo2-run -- <repo>/install/bin/ModOrganizer`. Instância de Skyrim SE das fases anteriores, com os mods da 4a (plugins `.esp`/`.esl` ativos) e o SKSE.

**Antes de começar:** anote se `~/.var/app/io.github.loot.loot/data/LOOT` existe (LOOT Flatpak) e qual é a ordem atual da aba de plugins.

| # | Passo | Esperado | Resultado |
|---|---|---|---|
| 1 | Aba de plugins → "Ordenar". | O diálogo do LOOT mostra o progresso (atualizar masterlist, ler plugins, ordenar) e termina com as mensagens do relatório. A masterlist fica em `~/.var/app/io.github.loot.loot/data/LOOT/games/Skyrim Special Edition/` (com o Flatpak) ou em `~/.local/share/LOOT/games/Skyrim Special Edition/`. | |
| 1b | No host: `ls <instância>/logs/`. | `lootcli.log` (linhas `[progress]`/`[info]`), `lootreport.json`, `loot-loadorder.txt`. | |
| 2 | Clicar em Aplicar (Apply). Conferir a aba de plugins; rodar o jogo pelo SKSE. | A ordem nova aparece na aba; o jogo abre e carrega o save normalmente. | |
| 3 | Só com o LOOT Flatpak: criar no LOOT uma regra de userlist (ex.: um plugin "load after" outro que hoje vem depois), fechar o LOOT, "Ordenar" de novo no MO2 e clicar em Aplicar (Apply). | A ordem respeita a regra. | |
| 4 | "Ordenar" e, durante o progresso, "Cancelar". No host: `<repo>/install/bin/mo2-vfs --list-mounts`. | O diálogo fecha cancelado; a ordem da aba não mudou; nada listado. | |
| 5 | Desligar a rede (Wi-Fi/cabo). Fechar e reabrir o MO2; "Ordenar". | Erro claro `Error downloading masterlist: …`; a masterlist anterior continua na pasta do item 1. | |
| 5b | Ainda sem rede: Configurações → Nexus/Geral → modo offline ligado; "Ordenar". Religar a rede e desligar o modo offline ao fim. | Ordena com a masterlist em cache, sem erro. | |

**Ao terminar:** registre aqui o resultado de cada item e, em "Observações", o que precisou de correção.

## Observações
```

- [ ] **Step 2: README, STATE e ROADMAP**

- `README.md`: troque o item "LOOT is unavailable, creating BSA archives is unavailable, there is no auto-update." por "LOOT sorts the plugins (lootcli with libloot 0.29.5, started through mo2-vfs; its data folder is the Flatpak LOOT's if there is one, else `~/.local/share/LOOT`). Creating BSA archives is unavailable, there is no auto-update.". A lista de pacotes da distrobox `mo2-run` (o `dnf -y install` da rota `fedora:44`) não muda: o lootcli roda no host, e quem compila usa o `build/Containerfile`.
- `ROADMAP.md`: linha 4b da tabela "Visão geral" → "🟡 Em andamento"; na seção "## Fase 4 — Ecossistema", depois da linha da Fase 4a, a linha `Fase 4b — Spec: [`docs/superpowers/specs/2026-10-18-phase4b-loot-design.md`](docs/superpowers/specs/2026-10-18-phase4b-loot-design.md) · Plano: [`docs/superpowers/plans/2026-10-18-phase4b-loot.md`](docs/superpowers/plans/2026-10-18-phase4b-loot.md) · Checklist manual: [`docs/testing/phase4b-manual-checklist.md`](docs/testing/phase4b-manual-checklist.md)`.
- `STATE.md`: "**Fase atual:** 4b — LOOT · **Status:** 🟡 implementação concluída; falta o CI, o teste manual e o merge · **Anterior:** 4a — Nexus, `nxm://` e keyring ✅"; "**Branch da Fase 4b:** `fase-4b-loot`"; seção nova "## Fase 4b — progresso" no topo (antes da 4a) com links de spec/plano/checklist e a tabela das Tasks 1–10 com os commits principais (`git log --oneline main..`); seção "## Limitações conhecidas da Fase 4b" antes da da 4a: listas de plugins dos INIs (`sTestFile`, Oblivion/Fallout 3/NV) não são lidas fora do Windows; na distrobox o lootcli roda no host e usa as bibliotecas Qt6Core, Boost.Locale 1.90 e libcurl de lá (o `build.sh` confere a lista); só Skyrim SE testado; valem as limitações das fases anteriores, menos "Sem LOOT". Nas limitações da Fase 1, risque "Sem LOOT" (`~~Sem LOOT~~ (resolvido na Fase 4b)`). Em "Decisões tomadas", uma linha 2026-10-18 com as decisões da spec §1.1. Em "Próximos passos", troque os itens da 4a pelos da 4b (push + CI, teste manual, merge).

- [ ] **Step 3: Commit**

```bash
git add docs/testing/phase4b-manual-checklist.md README.md STATE.md ROADMAP.md
git commit -m "docs: Phase 4b checklist, state and roadmap"
```

- [ ] **Step 4: Ponto de controle de CI (com o usuário)**

Peça autorização ao usuário para enviar o branch. Com ela:

```bash
git commit --allow-empty -m "ci: Phase 4b checkpoint [windows-ci]"
git push -u origin fase-4b-loot
```

Acompanhe com `gh run list --branch fase-4b-loot` e `gh run watch <id>`.
Expected: Linux verde, com `loot end-to-end test passed` e `vfs end-to-end test passed` no log (não `SKIPPED`), `100% tests passed` do `lootcli`, e o cache `libloot` salvo no fim do job; Windows verde. Rode o Linux uma segunda vez (`gh run rerun <id>`) e confira que o passo de build do libloot foi rápido (cache restaurado). Uma falha vira tarefa de correção antes da Task 10 (systematic-debugging); registre no STATE.

---

### Task 10: Teste manual com o usuário e fechamento

**Files:**
- Modify: `docs/testing/phase4b-manual-checklist.md`, `STATE.md`, `ROADMAP.md`, `docs/superpowers/specs/2026-10-18-phase4b-loot-design.md` (status)

- [ ] **Step 1: Pacote e roteiro**

Compile o branch na distrobox (`distrobox enter mo2-run -- ./build.sh`) e entregue ao usuário o checklist com os comandos exatos do ambiente dele (caminho de `install/bin/ModOrganizer` e da instância).

- [ ] **Step 2: Achados**

Cada item que falhar vira uma correção com o fluxo de systematic-debugging (causa raiz antes de corrigir), teste automático quando possível, commit próprio (`deps/modorganizer: Linux: …`, `deps/lootcli: Linux: …`, `mo2-vfs: …` ou `docs: …`) e uma linha em "Observações" do checklist e nas "Decisões tomadas" do `STATE.md` (data, o que mudou e por quê). Riscos já previstos na spec §4: biblioteca faltando no host, rede, cancelamento, helper do VFS caindo.

- [ ] **Step 3: Fechamento**

Com o checklist aprovado pelo usuário e o CI verde no último commit:
- spec: `**Status:** aprovada; implementada (teste manual aprovado em <data>)`;
- `ROADMAP.md`: 4b "✅ Concluída (<data>)";
- `STATE.md`: status da 4b concluída, próxima 4c; tabela com a Task 10 aprovada;
- merge em `main` só com a autorização do usuário (PR, como na 4a).

```bash
git add docs/testing/phase4b-manual-checklist.md STATE.md ROADMAP.md docs/superpowers/specs/2026-10-18-phase4b-loot-design.md
git commit -m "docs: Phase 4b manual test passed"
```

## Observações

- A Task 7 deixa o app sem linkar até a Task 8 (o stub de `Loot::waitForCompletion` sai da biblioteca `organizer_launch`); faça as duas em sequência e não envie o branch entre elas.
- `exitcodes::take()` é chamado na thread do LOOT: o mapa é protegido por mutex e o evento `exited` chega na thread principal, que roda o laço do diálogo do LOOT; o `processEvents` de `take()` na thread do LOOT não tem eventos e só espera (confirmado em `exitcodes_linux.cpp`, spec §3.4).
- O arquivo de saída é truncado duas vezes (pela `AsyncPipe::create()` e pelo `spawn` do `mo2-vfs`), sempre antes de o lootcli escrever; o descritor da `AsyncPipe` lê do começo.
