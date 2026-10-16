# Fase 2 — VFS FUSE (`mo2-vfs`) — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Programas Linux nativos iniciados pelo MO2 enxergam, via um FUSE próprio em Rust (`mo2-vfs`), a visão mesclada dos mods sobre as pastas do jogo, sem alterar a instalação do jogo.

**Architecture:** O crate `mo2-vfs` (superprojeto `ModOrganizerLinux`) é uma biblioteca (`name`, `mapping`, `layer`, `tree`, `ops`, `inodes`, `fs`, `session`, `state`, `watch`, `ipc`) mais o binário `mo2-vfs`, que o MO2 inicia com `QProcess` e controla por JSON-por-linha no stdin/stdout. No app (`deps/modorganizer`), o `LinuxVfsConnector` substitui o `NullVfsConnector`, a interface `VfsConnector` ganha `activate`/`watchProcessGroup`/`releaseIfIdle`/`isActive`/`waitInactive`/`setDeactivatedCallback` (vazios no Windows), e o "Executar" passa a iniciar programas Linux nativos com o VFS montado.

**Tech Stack:** Rust stable (edition 2024) com `fuser` 0.18, `parking_lot`, `serde`/`serde_json`, `rustix`, `tracing`; C++23/Qt 6 (`QProcess`, `QJsonDocument`); FUSE 3 (`fusermount3`); GoogleTest; GitHub Actions + container Fedora 44.

**Spec:** `docs/superpowers/specs/2026-10-15-phase2-vfs-fuse-design.md` (ler inteira antes de cada tarefa; as seções citadas como §N são dela)

## Global Constraints

- Um repositório só: `ModOrganizerLinux`, branch `fase-2-vfs-fuse`. O app está no subtree `deps/modorganizer` (o fork `FM0Ura/modorganizer` foi incorporado em 2026-10-15 e não recebe mais mudanças). Toda mudança dentro de um subtree vai em commit próprio com o assunto prefixado por `deps/<nome>:` (ex.: `deps/modorganizer: Linux: mo2-vfs connector`), como diz `deps/README.md`; onde o plano diz "(fork)", leia `deps/modorganizer`.
- O app (`deps/modorganizer`) continua compilando no Windows **contra o `uibase` upstream**: nada no fork pode depender de API nova do `uibase`; mudanças no `uibase` são aditivas ou restritas a arquivos Linux.
- Os novos métodos de `VfsConnector` têm implementação padrão vazia na própria interface; `usvfsconnector.*` não muda.
- Código Linux do fork em `src/linux/<nome>_linux.cpp`; `#ifdef _WIN32` só para mudanças de 1–3 linhas em código compartilhado.
- O CMake do fork **não** compila Rust. O `mo2-vfs` é encontrado em `QCoreApplication::applicationDirPath() + "/mo2-vfs"` ou pela variável de ambiente `MO2_VFS_BINARY` (que tem precedência).
- Rust: `cargo fmt --check` e `cargo clippy --all-targets -- -D warnings` limpos; nada de `unsafe` fora de `layer.rs`, `ops.rs` e `watch.rs` (chamadas `*at` e `/proc`), cada bloco com comentário `// SAFETY:`.
- Toda operação no disco real é relativa a um fd de camada (`openat`, `mkdirat`, `renameat`, `unlinkat`, `fstatat`), nunca por caminho absoluto: a camada original fica atrás do próprio mount.
- Mount: `MountOption::FSName("mo2-vfs")`, `MountOption::Subtype("mo2-vfs")` (tipo `fuse.mo2-vfs` no `mountinfo`), `DefaultPermissions`, sem `AllowOther`/`AutoUnmount`.
- Testes que exigem FUSE usam `common::require_fuse()` (Rust) ou `requireFuse()` (C++): sem `/dev/fuse` pulam com aviso; com `MO2_REQUIRE_FUSE=1` (definido no CI) falham.
- O diretório de estado é `$MO2_VFS_STATE_DIR` se definido, senão `$XDG_RUNTIME_DIR/mo2-vfs`; os testes sempre definem `MO2_VFS_STATE_DIR` num diretório temporário.
- Formatação C++: `clang-format` do fork. Mensagens ao usuário no fork em inglês no código-fonte (traduções ficam para a Fase 4), exceto a mensagem existente da Fase 3.
- Commits terminam com as linhas de atribuição da sessão; mensagens sem identificador de modelo.

## Ambiente de execução

O CI (GitHub Actions, container Fedora 44 com `--device /dev/fuse --cap-add SYS_ADMIN`) é a referência de "passou". No Bazzite, rodar dentro da distrobox Fedora 44 do README; ela vê `/dev/fuse` e `fusermount3` do host. Comandos `cargo` rodam em `mo2-vfs/`.

## Review Focus

1. **Nomes não-ASCII e bytes que não são UTF-8** em pastas de mods (`Ünïcödé Mod/`, `日本語.esp`, nome com byte `0xFF`): devem aparecer, abrir e ser encontrados com outra grafia de maiúsculas → teste em Task 3 (`tree_non_ascii_and_non_utf8_names`).
2. **Arquivo aberto durante um `update`** que tira o mod daquele arquivo da lista: o handle aberto continua lendo o conteúdo antigo até fechar; um `open` novo vê a árvore nova → teste em Task 7 (`open_handle_survives_update`).
3. **O mesmo arquivo aberto com duas grafias** (`A.TXT` para escrita, `a.txt` para leitura): os dois veem o mesmo conteúdo e o mesmo inode → teste em Task 6 (`same_file_two_spellings`).
4. **Links simbólicos dentro de um mod** (para fora do mod e quebrados): o link para arquivo aparece como o arquivo de destino (como o USVFS, que segue links); o quebrado é ignorado com aviso; nada trava → teste em Task 3 (`tree_symlinks`).
5. **Ponto de montagem que é um link simbólico** (ex.: `Data -> /mnt/disco2/Data`) ou caminho com `..`: a raiz é canonicalizada antes de abrir o fd e montar; o mount fica no destino real e o caminho do link mostra a visão virtual → teste em Task 7 (`symlinked_mountpoint`).

---

### Task 1: Esqueleto do crate, build e prova de FUSE no CI

**Files:**
- Create: `mo2-vfs/Cargo.toml`, `mo2-vfs/src/lib.rs`, `mo2-vfs/src/bin/mo2-vfs.rs`, `mo2-vfs/tests/common/mod.rs`, `mo2-vfs/tests/fuse_probe.rs`, `mo2-vfs/.gitignore`
- Modify: `build.sh` (alvo `mo2-vfs`), `build/Containerfile` (pacote `fuse3`), `.github/workflows/linux.yml` (flags do `docker run`, `MO2_REQUIRE_FUSE=1`), `README.md` (seção de build)

**Interfaces:**
- Produces: `cargo build --release` gera `target/release/mo2-vfs`; `./build.sh --only mo2-vfs [--tests]` instala em `install/bin/mo2-vfs`; `tests/common/mod.rs` exporta `pub fn require_fuse() -> bool` e `pub fn tempdir() -> tempfile::TempDir`.

- [ ] **Step 1: `Cargo.toml` e esqueleto**

```toml
[package]
name = "mo2-vfs"
version = "0.1.0"
edition = "2024"
license = "GPL-3.0-or-later"
description = "FUSE virtual file system of the Mod Organizer 2 Linux port"

[dependencies]
fuser = "0.18"
parking_lot = "0.12"
serde = { version = "1", features = ["derive"] }
serde_json = "1"
rustix = { version = "1", features = ["fs", "process"] }
libc = "0.2"
tracing = "0.1"
tracing-subscriber = { version = "0.3", features = ["fmt", "env-filter"] }
tracing-appender = "0.2"

[dev-dependencies]
tempfile = "3"

[profile.release]
debug = 1
```

`src/lib.rs`: só `//! mo2-vfs: ...` (doc do crate, uma linha por módulo planejado, listando-os como na §2.2). `src/bin/mo2-vfs.rs`: `fn main() { println!("{{\"event\":\"hello\",\"version\":\"{}\"}}", env!("CARGO_PKG_VERSION")); }` (substituído na Task 8). `.gitignore`: `/target`.

- [ ] **Step 2: `tests/common/mod.rs`**

```rust
#![allow(dead_code)]
use std::path::Path;

/// true when FUSE can be used; false (skip) when it can't, unless MO2_REQUIRE_FUSE=1
pub fn require_fuse() -> bool {
    let available = Path::new("/dev/fuse").exists()
        && std::process::Command::new("fusermount3").arg("--version").output().is_ok();
    if !available {
        if std::env::var("MO2_REQUIRE_FUSE").as_deref() == Ok("1") {
            panic!("FUSE is required (MO2_REQUIRE_FUSE=1) but /dev/fuse or fusermount3 is missing");
        }
        eprintln!("skipped: FUSE is not available");
    }
    available
}

pub fn tempdir() -> tempfile::TempDir {
    tempfile::Builder::new().prefix("mo2-vfs-test").tempdir().unwrap()
}
```

- [ ] **Step 3: teste de prova de FUSE (falha antes do fs mínimo existir)**

`tests/fuse_probe.rs`: um `fuser::Filesystem` mínimo dentro do próprio teste (raiz com um arquivo `hello.txt` de conteúdo `"hello\n"`, inode 2; implementar `lookup`, `getattr`, `readdir`, `open`, `read`), montado com `fuser::spawn_mount` num `tempdir()`; o teste lê `mnt/hello.txt` e confere `"hello\n"`, depois `umount_and_join()`. Começa com `if !common::require_fuse() { return; }`.

Run: `cargo test --test fuse_probe` — Expected: PASS no Bazzite/distrobox (ou "skipped" sem FUSE). Ajustar assinaturas conforme a doc do `fuser` 0.18 (`INodeNo`, `Config { mount_options, acl, n_threads, clone_fd }`, métodos `&self`).

- [ ] **Step 4: `build.sh`**

Adicionar `mo2-vfs` a `ALL_TARGETS` **antes** de `modorganizer` e:

```bash
# mo2-vfs is the FUSE virtual file system (Rust, ./mo2-vfs), a program of its own
# started by the application; with --tests its unit and integration tests run (the
# ones that need FUSE are skipped without /dev/fuse, unless MO2_REQUIRE_FUSE=1)
build_mo2_vfs() {
    local src="$ROOT/mo2-vfs"
    log "build mo2-vfs"
    cargo build --release --locked --manifest-path "$src/Cargo.toml" \
        --target-dir "$BUILD_OUT/mo2-vfs"
    if [[ $TESTS == 1 ]]; then
        cargo test --release --locked --manifest-path "$src/Cargo.toml" \
            --target-dir "$BUILD_OUT/mo2-vfs"
    fi
    install -Dm755 "$BUILD_OUT/mo2-vfs/release/mo2-vfs" "$INSTALL/bin/mo2-vfs"
}
```

Gerar e versionar `mo2-vfs/Cargo.lock` (`cargo generate-lockfile`).

- [ ] **Step 5: container e CI**

`build/Containerfile`: acrescentar `fuse3` à lista do `dnf install` (ao lado de `fuse3-devel`). `.github/workflows/linux.yml`: nos dois `docker run`, acrescentar `--device /dev/fuse --cap-add SYS_ADMIN --security-opt apparmor:unconfined -e MO2_REQUIRE_FUSE=1`. README: uma linha na seção de build citando o alvo `mo2-vfs` e o requisito de `/dev/fuse` para os testes.

- [ ] **Step 6: verificar**

Run: `./build.sh --only mo2-vfs --tests` (na distrobox) — Expected: `fuse_probe` PASS, `install/bin/mo2-vfs` imprime o `hello`.
Push e checar a execução do CI Linux: `fuse_probe` deve passar **no runner** (sem "skipped"). Se o runner recusar o mount, trocar as flags por `--privileged` e registrar o motivo num comentário no workflow.

- [ ] **Step 7: Commit** — `mo2-vfs: crate skeleton, build target and FUSE probe in CI`

---

### Task 2: Nomes e agrupamento dos mappings em raízes

**Files:**
- Create: `mo2-vfs/src/name.rs`, `mo2-vfs/src/mapping.rs`
- Modify: `mo2-vfs/src/lib.rs` (`pub mod name; pub mod mapping;`)

**Interfaces:**
- Produces:
  - `name::Key` (`#[derive(Clone, PartialEq, Eq, Hash, Debug, PartialOrd, Ord)] pub struct Key(Vec<u8>)`); `pub fn fold(name: &OsStr) -> Key`; `pub fn fold_path(rel: &Path) -> Key` (componentes dobrados, unidos por `/`; caminho vazio = raiz).
  - `mapping::Mapping { source: PathBuf, destination: PathBuf, is_directory: bool, create_target: bool }` (`Deserialize`/`Serialize`, nomes de campo exatamente esses).
  - `mapping::LayerSpec { source: PathBuf, target: PathBuf /* relativo à raiz, "" = raiz */, is_directory: bool, create_target: bool, priority: usize /* índice no vetor original */ }`.
  - `mapping::RootSpec { mountpoint: PathBuf /* canônico */, layers: Vec<LayerSpec> /* ordem crescente de prioridade */ }`.
  - `pub fn plan_roots(mappings: &[Mapping]) -> Result<Vec<RootSpec>, MappingError>`; `MappingError::{Relative(PathBuf), ParentDir(PathBuf)}` com `Display`.

- [ ] **Step 1: testes de `name`** (em `name.rs`, `#[cfg(test)]`)

```rust
#[test] fn fold_is_case_insensitive() {
    assert_eq!(fold(OsStr::new("Textures")), fold(OsStr::new("TEXTURES")));
    assert_eq!(fold(OsStr::new("Ünïcödé")), fold(OsStr::new("üNÏCÖDÉ")));
    assert_ne!(fold(OsStr::new("a")), fold(OsStr::new("b")));
}
#[test] fn fold_non_utf8_lowercases_ascii_bytes_only() {
    use std::os::unix::ffi::OsStrExt;
    let a = OsStr::from_bytes(b"AB\xff"); let b = OsStr::from_bytes(b"ab\xff");
    assert_eq!(fold(a), fold(b));
}
#[test] fn fold_path_joins_components() {
    assert_eq!(fold_path(Path::new("Textures/Rock.DDS")), fold_path(Path::new("textures/rock.dds")));
    assert_eq!(fold_path(Path::new("")).0, b"".to_vec());
}
```

- [ ] **Step 2:** `cargo test name::` — FAIL (não compila).
- [ ] **Step 3: implementar `name.rs`**: UTF-8 válido → `str::to_lowercase()` (mapeamento Unicode completo) em bytes; senão, `to_ascii_lowercase()` nos bytes. `fold_path` junta os `fold` de cada `Component::Normal` com `b'/'`.
- [ ] **Step 4:** `cargo test name::` — PASS.

- [ ] **Step 5: testes de `mapping`**

```rust
fn m(src: &str, dst: &str, dir: bool, create: bool) -> Mapping { /* constrói */ }

#[test] fn one_root_per_destination() {
    let t = tempdir(); // cria t/Data e t/Saves
    let roots = plan_roots(&[
        m("/mods/A", &format!("{}/Data", t), true, false),
        m("/mods/B", &format!("{}/Data", t), true, false),
        m("/profiles/p/saves", &format!("{}/Saves", t), true, true),
    ]).unwrap();
    assert_eq!(roots.len(), 2);
    let data = roots.iter().find(|r| r.mountpoint.ends_with("Data")).unwrap();
    assert_eq!(data.layers.iter().map(|l| l.source.clone()).collect::<Vec<_>>(),
               vec![PathBuf::from("/mods/A"), PathBuf::from("/mods/B")]);
    assert!(data.layers.iter().all(|l| l.target.as_os_str().is_empty()));
}
#[test] fn nested_destination_joins_outer_root() {
    // dir mapping to <t>/Data and dir mapping to <t>/Data/textures -> one root, target "textures"
}
#[test] fn file_mapping_inside_root_and_alone() {
    // file mapping to <t>/Data/x.esp joins the Data root with target "x.esp";
    // file mapping to <t>/Other/y.ini without a directory mapping makes <t>/Other a root
}
#[test] fn priority_is_input_order() { /* layers[i].priority crescente e igual ao índice de entrada */ }
#[test] fn rejects_relative_and_dotdot() {
    assert!(matches!(plan_roots(&[m("mods/A", "/g/Data", true, false)]), Err(MappingError::Relative(_))));
    assert!(matches!(plan_roots(&[m("/mods/A", "/g/../Data", true, false)]), Err(MappingError::ParentDir(_))));
}
#[test] fn mountpoint_is_canonical() {
    // <t>/link -> <t>/Data (symlink); mapping para <t>/link; mountpoint == canonicalize(<t>/Data)
}
```

- [ ] **Step 6:** `cargo test mapping::` — FAIL.
- [ ] **Step 7: implementar `plan_roots`**: (1) validar absolutos e sem `..` (`ParentDir` também para `..` na origem); (2) canonicalizar o destino quando ele existir (senão canonicalizar o pai existente mais próximo e juntar o resto); (3) destinos de mappings de diretório ordenados por número de componentes; um destino é raiz se nenhuma raiz já escolhida for prefixo dele; (4) cada mapping vai para a raiz que é prefixo do seu destino, com `target = destino − raiz`; mapping de arquivo sem raiz cria raiz no diretório-pai; (5) camadas por raiz em ordem de `priority`.
- [ ] **Step 8:** `cargo test mapping::` — PASS. `cargo clippy --all-targets -- -D warnings` limpo.
- [ ] **Step 9: Commit** — `mo2-vfs: name folding and mapping roots`

---

### Task 3: Camadas e construção da árvore

**Files:**
- Create: `mo2-vfs/src/layer.rs`, `mo2-vfs/src/tree.rs`
- Modify: `mo2-vfs/src/lib.rs`

**Interfaces:**
- Consumes: `name::{Key, fold, fold_path}`, `mapping::{RootSpec, LayerSpec}`.
- Produces:
  - `layer::LayerId = usize` (0 = original). `layer::Layer { id: LayerId, dir: OwnedFd /* O_RDONLY|O_DIRECTORY|O_CLOEXEC */, file: Option<OsString> /* mapping de arquivo: nome do arquivo dentro de dir */, target: PathBuf, create: bool, original: bool, source: PathBuf /* só para log */ }`.
  - `layer::open_layers(root: &RootSpec) -> io::Result<Layers>` — abre o fd do mountpoint (camada 0, criando o diretório se não existir e registrando isso em `Layers::created_mountpoint: bool`) e um fd por `LayerSpec`; origem inexistente → camada pulada com `tracing::warn!`. Retorna `Layers { list: Vec<Layer>, created_mountpoint: bool }`.
  - `layer::Skip { suffixes: Vec<String> /* dobrados */, directories: Vec<Key> }` com `fn skips_file(&self, name: &OsStr) -> bool` e `fn skips_dir(&self, name: &OsStr) -> bool`.
  - `tree::Kind::{File, Dir}` (links são seguidos, §4); `tree::Origin { layer: LayerId, rel: PathBuf /* relativo ao fd da camada */ }`.
  - `tree::Node { name: OsString /* grafia exibida */, kind: Kind, origin: Origin /* camada vencedora */, dir_origins: Vec<Origin> /* só Dir: todas as camadas que têm o dir, menor→maior */, children: BTreeMap<Key, NodeId> }`; `type NodeId = usize`.
  - `tree::Tree::build(layers: &Layers, skip: &Skip) -> Tree`; `fn root(&self) -> NodeId`; `fn node(&self, id: NodeId) -> &Node`; `fn lookup(&self, vpath: &Path) -> Option<NodeId>`; `fn child(&self, dir: NodeId, name: &OsStr) -> Option<NodeId>`; `fn children(&self, dir: NodeId) -> impl Iterator<Item = (NodeId, &Node)>`; `fn path_of(&self, id: NodeId) -> PathBuf` (grafia exibida); `fn len(&self) -> usize`.

- [ ] **Step 1: testes** (em `tree.rs`; helper `fixture(&[(&str /*layer dir*/, &[&str] /*files rel*/ )])` que cria diretórios num tempdir, monta `RootSpec` com o primeiro como mountpoint/original e os demais como camadas de diretório, e chama `open_layers` + `Tree::build`)

```rust
#[test] fn tree_priority_last_wins() {
    // original: textures/rock.dds, Skyrim.esm ; A: textures/rock.dds, meshes/tree.nif ; B: textures/rock.dds
    let t = fixture(...);
    assert_eq!(origin_layer(&t, "textures/rock.dds"), 2); // B
    assert_eq!(origin_layer(&t, "meshes/tree.nif"), 1);
    assert_eq!(origin_layer(&t, "Skyrim.esm"), 0);
    assert_eq!(names(&t, ""), ["meshes", "Skyrim.esm", "textures"]); // ordem por Key
}
#[test] fn tree_case_insensitive_merge_and_display_spelling() {
    // A: Textures/a.dds ; B: textures/B.DDS -> um diretório só; exibido "textures" (camada vencedora B)
    // lookup("TEXTURES/b.dds") encontra B.DDS; dir_origins tem as duas camadas
}
#[test] fn tree_dir_vs_file_higher_wins() {
    // A: foo/ (dir com x) ; B: foo (arquivo) -> foo é File da camada B, foo/x não existe
}
#[test] fn tree_file_mapping_overrides_one_path() {
    // camada de mapping de arquivo: source <t>/extra/custom.ini, target "Skyrim.ini" -> lookup("skyrim.ini") vem dessa camada, rel "custom.ini"
}
#[test] fn tree_target_subdirectory() { /* camada com target "textures" mescla dentro de textures/ */ }
#[test] fn tree_skip_suffixes_and_directories() {
    // Skip { suffixes: [".mohidden"], directories: [fold("fomod")] } -> a.esp.mohidden e fomod/ não aparecem
}
#[test] fn tree_missing_source_is_skipped() { /* LayerSpec com source inexistente: árvore construída, sem a camada */ }
#[test] fn tree_non_ascii_and_non_utf8_names() {
    // "Ünïcödé Mod/日本語.esp" e um arquivo com byte 0xFF no nome: aparecem com a grafia exata,
    // lookup com outra caixa ("ÜNÏCÖDÉ MOD/日本語.ESP") encontra
}
#[test] fn tree_symlinks() {
    // mod com link para arquivo fora do mod -> Kind::File (segue o link); link quebrado -> ausente, sem panic
}
```

- [ ] **Step 2:** `cargo test tree::` — FAIL.
- [ ] **Step 3: implementar `layer.rs`** (`rustix::fs::openat` com `OFlags::RDONLY|DIRECTORY|CLOEXEC`; mapping de arquivo abre o diretório-pai e guarda o nome) e **`tree.rs`**: varredura recursiva por camada em ordem crescente (`rustix::fs::Dir` sobre `openat` de cada subdiretório; `fstatat` sem `AT_SYMLINK_NOFOLLOW` para seguir links, e `ENOENT` nele = link quebrado → `warn!` e pula); cada entrada sobrescreve a anterior com a mesma `Key` (arquivo substitui; dir sobre dir mescla e acrescenta a `dir_origins`; tipos diferentes → a camada maior substitui o nó inteiro). Camadas de arquivo inserem um só nó em `target`. `Tree` guarda `nodes: Vec<Node>`; nós removidos (Task 4) viram tombstones fora de `children`.
- [ ] **Step 4:** `cargo test tree::` — PASS.
- [ ] **Step 5: Commit** — `mo2-vfs: layers and merged tree`

---

### Task 4: Operações sobre a árvore e o disco (semântica da §4, sem FUSE)

**Files:**
- Create: `mo2-vfs/src/ops.rs`
- Modify: `mo2-vfs/src/tree.rs` (mutações), `mo2-vfs/src/lib.rs`

**Interfaces:**
- Consumes: `tree::*`, `layer::*`.
- Produces (todas recebem `&mut Tree` e `&Layers`; erros como `rustix::io::Errno`):
  - `tree::Tree::{insert(parent: NodeId, node: Node) -> NodeId, remove(id: NodeId), mask(vpath: &Path), masks(&self) -> &BTreeSet<Key>, apply_masks(&mut self, masks: &BTreeSet<Key>), move_node(id: NodeId, new_parent: NodeId, new_name: &OsStr)}` — `remove` + `mask` = exclusão; `insert` num caminho mascarado tira a máscara.
  - `ops::create_layer_for(tree: &Tree, layers: &Layers, vpath: &Path) -> Option<LayerId>` — camada com `create` de maior prioridade cujo `target` é prefixo de `vpath`.
  - `ops::open(layers, node: &Node, flags: OFlags) -> Result<OwnedFd, Errno>`.
  - `ops::create_file(tree, layers, parent: NodeId, name: &OsStr, mode: Mode, flags: OFlags) -> Result<(NodeId, OwnedFd), Errno>`.
  - `ops::mkdir(tree, layers, parent, name, mode) -> Result<NodeId, Errno>`.
  - `ops::unlink(tree, layers, parent, name) -> Result<(), Errno>`; `ops::rmdir(...)` idem.
  - `ops::rename(tree, layers, parent, name, new_parent, new_name, flags: RenameFlags) -> Result<(), Errno>`.
  - `ops::ensure_parents(layers, layer: LayerId, rel_dir: &Path) -> Result<(), Errno>` (`mkdirat` -p relativo ao fd, `EEXIST` ignorado).

- [ ] **Step 1: testes** (fixture da Task 3 com `overwrite` como última camada `create: true`; cada teste confere **disco** e **árvore**)

```rust
#[test] fn create_goes_to_create_layer_with_parents() {
    // create "new/dir/novo.txt" sob raiz -> existe <overwrite>/new/dir/novo.txt; lookup encontra; origin.layer == overwrite
}
#[test] fn create_without_create_layer_is_erofs() { /* fixture sem create -> Err(Errno::ROFS) */ }
#[test] fn unlink_mod_file_deletes_and_masks() {
    // A e B têm textures/rock.dds; unlink -> arquivo de B apagado no disco; lookup == None (A não reaparece); masks contém a key
}
#[test] fn unlink_original_only_masks() {
    // unlink Skyrim.esm -> arquivo original continua no disco; lookup == None
}
#[test] fn rmdir_merged_dir_requires_virtual_empty() {
    // dir com filhos -> NOTEMPTY; depois de apagar os filhos -> remove os dirs reais vazios das camadas não-originais e mascara
}
#[test] fn create_after_unlink_clears_mask() { /* unlink e create no mesmo caminho -> arquivo novo no overwrite, visível */ }
#[test] fn rename_within_create_layer() {
    // create save.tmp, rename -> save.ess: <overwrite>/save.ess existe, save.tmp não; lookup("SAVE.ESS") ok
}
#[test] fn rename_mod_file_moves_in_its_layer_and_masks_destination() {
    // A: a.txt, B: b.txt; rename A/a.txt -> b.txt: <A>/b.txt existe, <A>/a.txt não, <B>/b.txt intacto mas mascarado; lookup(b.txt).origin.layer == A
}
#[test] fn rename_original_file_copies_to_create_layer() {
    // rename Skyrim.esm -> Skyrim.bak: original intacto; <overwrite>/Skyrim.bak com o mesmo conteúdo, modo e mtime; Skyrim.esm mascarado
}
#[test] fn rename_multi_layer_dir_or_original_dir_is_exdev() { /* Err(Errno::XDEV) */ }
#[test] fn rename_file_mapping_is_exdev() { /* Err(Errno::XDEV) */ }
#[test] fn rename_exchange_and_whiteout_are_einval() { /* Err(Errno::INVAL) */ }
#[test] fn masks_survive_rebuild() {
    // unlink, depois Tree::build de novo + apply_masks(antigas) -> continua ausente
}
```

- [ ] **Step 2:** `cargo test ops::` — FAIL.
- [ ] **Step 3: implementar** seguindo a tabela da §4 linha a linha (cópia da camada original: `openat` origem `RDONLY`, `openat` destino `WRONLY|CREAT|EXCL`, `copy_file_range` em laço com fallback `read`/`write`, depois `fchmod` e `futimens` com os tempos da origem; destino existente em outra camada é mascarado antes de inserir o nó novo).
- [ ] **Step 4:** `cargo test ops::` — PASS.
- [ ] **Step 5: Commit** — `mo2-vfs: file operations on the merged tree`

---

### Task 5: Adaptador FUSE — leitura

**Files:**
- Create: `mo2-vfs/src/inodes.rs`, `mo2-vfs/src/fs.rs`, `mo2-vfs/tests/fs_read.rs`
- Modify: `mo2-vfs/src/lib.rs`, `mo2-vfs/tests/common/mod.rs` (`Fixture`)

**Interfaces:**
- Consumes: `tree`, `ops`, `layer`.
- Produces:
  - `inodes::Inodes` — `fn ino_for(&mut self, key: &Key) -> u64` (raiz = 1, estável enquanto o mount existir), `fn key_of(&self, ino: u64) -> Option<&Key>`, `fn rekey_prefix(&mut self, old: &Key, new: &Key)` (renomeação de diretório), `fn known(&self) -> impl Iterator<Item = (u64, &Key)>`.
  - `fs::Vfs::new(layers: Layers, tree: Tree, options: VfsOptions) -> Vfs`; `VfsOptions { ttl: Duration /* 60 s */, try_passthrough: bool }`; `impl fuser::Filesystem for Vfs`; `pub fn shared(&self) -> Arc<VfsShared>` (estado compartilhado com a sessão: `tree: RwLock<Tree>`, `inodes: Mutex<Inodes>`, `layers`, `handles: Mutex<HashMap<u64, Arc<OwnedFd>>>`, `passthrough: AtomicBool`).
  - `tests/common::Fixture` — `Fixture::new(&[("original", &[...]), ("A", &[...]), ...])` cria as pastas com arquivos cujo conteúdo é o próprio caminho relativo + nome da camada (ex.: `"A:textures/rock.dds"`), a última camada `overwrite` com `create: true`; `fixture.mount() -> Mounted` (monta a raiz com `Vfs` + `fuser::spawn_mount`, `n_threads: Some(4)`) e `Mounted::path()`, `Mounted::drop` desmonta.

- [ ] **Step 1: testes de integração** (`tests/fs_read.rs`, todos começando com `if !common::require_fuse() { return; }`)

```rust
#[test] fn reads_winner_content() {
    let m = Fixture::skyrim().mount();
    assert_eq!(fs::read_to_string(m.path().join("textures/rock.dds")).unwrap(), "B:textures/rock.dds");
    assert_eq!(fs::read_to_string(m.path().join("Skyrim.esm")).unwrap(), "original:Skyrim.esm");
}
#[test] fn lists_merged_directory() {
    let mut names: Vec<_> = fs::read_dir(m.path()).unwrap().map(|e| e.unwrap().file_name()).collect();
    names.sort(); assert_eq!(names, ["Skyrim.esm", "meshes", "textures"]);
}
#[test] fn lookup_any_case() { assert!(m.path().join("TEXTURES/ROCK.DDS").exists()); }
#[test] fn original_is_visible_under_the_mount() {
    // o mountpoint tinha Skyrim.esm antes de montar: continua visível pela camada 0
}
#[test] fn metadata_comes_from_real_file() { /* len e mtime iguais aos do arquivo real da camada vencedora */ }
#[test] fn symlinks_are_followed() { /* link de mod para arquivo: symlink_metadata() no mount é arquivo regular com o conteúdo do alvo */ }
#[test] fn large_file_read_at_offsets() { /* arquivo de 8 MiB, leituras em offsets aleatórios == conteúdo real */ }
#[test] fn passthrough_falls_back_to_pread() {
    // VfsOptions { try_passthrough: true }: sem CAP_SYS_ADMIN o primeiro open recusa o backing e
    // desliga a tentativa; a leitura funciona igual; shared().passthrough == false depois
}
```

- [ ] **Step 2:** `cargo test --test fs_read` — FAIL.
- [ ] **Step 3: implementar** `lookup`, `getattr`, `readdir` (com `.` e `..`, offsets estáveis = índice na ordem de `children`), `opendir`/`releasedir`, `open` (via `ops::open`; passthrough: `reply.open_backing(&fd)`; se `Err`, `passthrough.store(false)` e `reply.opened`), `read` (`rustix::io::pread`), `release`, `statfs` (`fstatvfs` do fd da camada de criação). Atributos: `fstatat` no fd da camada + `rel`, `ino` de `Inodes`. Cada handler envolve o corpo em `std::panic::catch_unwind(AssertUnwindSafe(...))`; `panic` → `reply.error(EIO)` + `tracing::error!`.
- [ ] **Step 4:** `cargo test --test fs_read` — PASS.
- [ ] **Step 5: Commit** — `mo2-vfs: FUSE read path`

---

### Task 6: Adaptador FUSE — escrita, criação, exclusão, renomeação

**Files:**
- Create: `mo2-vfs/tests/fs_write.rs`
- Modify: `mo2-vfs/src/fs.rs`

**Interfaces:**
- Consumes: `ops::*`, `Inodes::rekey_prefix`.
- Produces: `write`, `setattr` (tamanho, modo, tempos), `fsync`, `flush`, `create`, `mkdir`, `unlink`, `rmdir`, `rename` (flags repassadas), `symlink`/`link`/`mknod` → `EPERM`, `setxattr`/`getxattr`/`listxattr`/`removexattr` → `ENOTSUP`.

- [ ] **Step 1: testes de integração** (`tests/fs_write.rs`; após cada operação, conferir **o disco das camadas** pelo caminho real, não pelo mount)

```rust
#[test] fn edit_mod_file_changes_mod() { /* escreve em meshes/tree.nif -> <A>/meshes/tree.nif mudou */ }
#[test] fn edit_original_file_changes_original() { /* §1.1: conteúdo do original muda */ }
#[test] fn truncate_and_chmod_reach_real_file() {}
#[test] fn new_file_goes_to_overwrite() { /* echo > novo.txt -> <overwrite>/novo.txt; original intacto */ }
#[test] fn mkdir_p_goes_to_overwrite() {}
#[test] fn delete_mod_file_and_lower_stays_hidden() {}
#[test] fn delete_original_keeps_game_file() { /* some do mount; <original>/Skyrim.esm intacto no disco */ }
#[test] fn save_tmp_rename_pattern() {
    // escreve SaveX.tmp, rename SaveX.tmp -> SaveX.ess (como o jogo): só <overwrite>/SaveX.ess
}
#[test] fn rename_original_copies() {}
#[test] fn rename_merged_dir_is_exdev_and_mv_still_works() {
    // std::fs::rename -> Err(raw_os_error == EXDEV); `mv` (Command) faz cópia+exclusão e termina com sucesso
}
#[test] fn same_file_two_spellings() {
    // abre "A.TXT" para escrita e "a.txt" para leitura: mesmo ino (metadata().ino()), leitura vê o que foi escrito
}
#[test] fn directory_alias_by_spelling() {
    // stat "Textures", depois "TEXTURES", depois readdir de "textures": sem EIO/ESTALE, mesmo ino
}
#[test] fn symlink_and_hardlink_refused() {}
```

- [ ] **Step 2:** `cargo test --test fs_write` — FAIL.
- [ ] **Step 3: implementar** cada handler pegando o lock de escrita de `tree` só durante a chamada de `ops::*` (nunca durante `pread`/`pwrite`); após `rename` de diretório, `inodes.rekey_prefix`. Se `directory_alias_by_spelling` falhar com `EIO`/`ESTALE`, no `lookup` por grafia diferente da exibida chamar `Notifier::inval_entry(parent, nome_exibido)` antes de responder (como o `ciopfs`) — o `Notifier` vem da sessão (Task 7) via `VfsShared::notifier: OnceLock<fuser::Notifier>`.
- [ ] **Step 4:** `cargo test --test fs_write --test fs_read` — PASS.
- [ ] **Step 5: Commit** — `mo2-vfs: FUSE write path`

---

### Task 7: Sessão — várias raízes, atualização a quente, desmontagem, estado

**Files:**
- Create: `mo2-vfs/src/session.rs`, `mo2-vfs/src/state.rs`, `mo2-vfs/tests/session.rs`
- Modify: `mo2-vfs/src/lib.rs`, `mo2-vfs/src/fs.rs` (notifier)

**Interfaces:**
- Consumes: `mapping::plan_roots`, `layer::open_layers`, `tree::Tree::build`, `fs::Vfs`.
- Produces:
  - `session::Session::mount(mappings: &[Mapping], skip: Skip) -> Result<Session, SessionError>`; `fn roots(&self) -> Vec<PathBuf>`; `fn update(&mut self, mappings: &[Mapping], skip: Skip) -> Result<(), SessionError>`; `fn unmount(self) -> Result<(), SessionError>`; `fn stats(&self) -> Stats { nodes: usize, masks: usize }`.
  - `SessionError { code: ErrorCode, message: String, path: Option<PathBuf> }`; `ErrorCode::{FuseUnavailable, MountBusy, BadMapping, Io}` serializado como `fuse_unavailable` etc.
  - `state::StateFile::write(pid: u32, roots: &[PathBuf])`, `state::StateFile::remove(pid)`, `state::cleanup_stale() -> Vec<(PathBuf, Result<(), String>)>`, `state::mounted_mo2_vfs() -> Vec<PathBuf>` (lê `/proc/self/mountinfo`, tipo `fuse.mo2-vfs`), `state::state_dir() -> PathBuf`.
  - `session::unmount_path(path: &Path) -> Result<(), String>`: `fusermount3 -u` repetido a cada 250 ms por até 5 s em `EBUSY`; depois `fusermount3 -uz`.

- [ ] **Step 1: testes de integração** (`tests/session.rs`; `MO2_VFS_STATE_DIR` num tempdir em cada teste)

```rust
#[test] fn mounts_every_root() { /* mappings para <t>/Data e <t>/Saves -> os dois aparecem em mounted_mo2_vfs() */ }
#[test] fn missing_root_is_created_and_removed() { /* <t>/NewDir não existe; montado; após unmount, NewDir não existe */ }
#[test] fn update_swaps_tree_and_invalidates() {
    // stat textures/rock.dds (B); update sem B -> leitura imediata devolve A (sem esperar o TTL de 60 s)
}
#[test] fn masks_survive_update() {}
#[test] fn open_handle_survives_update() {
    // abre textures/rock.dds (B), update sem B, lê pelo handle aberto -> conteúdo de B; open novo -> A
}
#[test] fn busy_unmount_falls_back_to_lazy() {
    // processo filho (`sleep 30` com cwd dentro do mount); unmount retorna Ok em <= 6 s; mount some do mountinfo
}
#[test] fn mount_over_already_mounted_root_is_busy() { /* segunda Session sobre a mesma raiz -> MountBusy */ }
#[test] fn bad_mapping_error() { /* origem relativa -> BadMapping */ }
#[test] fn state_file_written_and_removed() {}
#[test] fn symlinked_mountpoint() {
    // <t>/link -> <t>/Data; mapping para <t>/link: mount em <t>/Data; <t>/link/arquivo-do-mod existe
}
```

- [ ] **Step 2:** `cargo test --test session` — FAIL.
- [ ] **Step 3: implementar** (mensagem de `FuseUnavailable`: o que falta — `/dev/fuse` ou `fusermount3` — e, se `/run/.containerenv` ou `/.dockerenv` existir, que o container/distrobox precisa ter `/dev/fuse`): `mount` = `plan_roots` → por raiz `open_layers` (o fd da camada 0 é aberto **antes** de montar) → `Tree::build` → `fuser::spawn_mount`; guarda o `BackgroundSession` e põe `session.notifier()` em `VfsShared::notifier`; falha no meio desmonta o que já montou. `/dev/fuse` ausente ou `fusermount3` ausente → `FuseUnavailable`; raiz que já está em `mounted_mo2_vfs()` → `MountBusy`. `update` = reconstruir árvores fora do lock, `apply_masks`, trocar sob o lock de escrita, e então, para cada `(ino, key)` em `inodes.known()` cuja origem mudou ou sumiu: `notifier.inval_inode(ino, 0, 0)` e `notifier.inval_entry(ino_do_pai, nome)`. `unmount` = `unmount_path` por raiz, `join` dos `BackgroundSession`, remove mountpoints criados vazios, remove o arquivo de estado.
- [ ] **Step 4:** `cargo test --test session` — PASS.
- [ ] **Step 5: Commit** — `mo2-vfs: mount sessions, hot update and cleanup state`

---

### Task 8: Protocolo, binário `mo2-vfs` e acompanhamento de grupos

**Files:**
- Create: `mo2-vfs/src/ipc.rs`, `mo2-vfs/src/watch.rs`, `mo2-vfs/tests/protocol.rs`
- Modify: `mo2-vfs/src/bin/mo2-vfs.rs`, `mo2-vfs/src/lib.rs`

**Interfaces:**
- Consumes: `session::*`, `state::*`.
- Produces:
  - `ipc::Request` (`#[serde(tag = "op", rename_all = "snake_case")]`): `Mount { mappings: Vec<Mapping>, skip_file_suffixes: Vec<String>, skip_directories: Vec<String> }`, `Update { … mesmos }`, `Watch { pgid: i32 }`, `Unmount`, `Status`.
  - `ipc::Event` (`#[serde(tag = "event", rename_all = "snake_case")]`): `Hello { version: String }`, `Ready { roots: Vec<PathBuf> }`, `Ok`, `Error { code: ErrorCode, message: String, path: Option<PathBuf> }`, `Unmounted`, `Status { roots, nodes, masks, groups: Vec<i32> }`.
  - `watch::group_alive(pgid: i32) -> bool` — percorre `/proc/[0-9]*/stat`: algum processo com campo `pgrp == pgid` e estado diferente de `Z`.
  - CLI: `mo2-vfs [--log <arquivo>] [--log-level trace|debug|info|warn|error]` (modo servidor, stdin/stdout) e `mo2-vfs --cleanup` (imprime um JSON por raiz tratada e sai com 0, ou 1 se alguma raiz não pôde ser desmontada).
  - Laço: thread de leitura do stdin → canal; thread de vigilância a cada 500 ms; regras da §5.1/§5.4 (com grupos e todos mortos → desmonta, emite `unmounted`; stdin EOF sem grupo vivo → desmonta e sai com 0; stdin EOF com grupo vivo → espera, desmonta, sai). `SIGTERM`/`SIGINT` → desmonta e sai.

- [ ] **Step 1: testes** (`tests/protocol.rs`; usa `env!("CARGO_BIN_EXE_mo2-vfs")`, escreve pedidos no stdin e lê eventos linha a linha com timeout de 10 s; helper `Client::start()`, `client.send(json!(...))`, `client.next_event()`)

```rust
#[test] fn hello_then_mount_ready() {}
#[test] fn update_and_status() { /* status.nodes muda depois do update */ }
#[test] fn error_bad_mapping() { /* event error, code "bad_mapping", processo continua respondendo */ }
#[test] fn auto_unmount_when_watched_group_ends() {
    // inicia `sh -c "sleep 1"` com process_group(0); watch {pgid}; recebe "ok", e em <= 3 s um "unmounted" espontâneo
}
#[test] fn zombie_does_not_hold_mount() {
    // filho do teste que termina e NÃO é reaped (sem wait); group_alive(pgid) == false; mount liberado
}
#[test] fn stdin_eof_without_group_unmounts_and_exits() {}
#[test] fn stdin_eof_with_live_group_keeps_mount_until_group_ends() {
    // watch de `sleep 2`; fecha stdin; mount continua por ~2 s; depois desmonta e o processo sai com 0
}
#[test] fn kill9_then_cleanup() {
    // mount, kill -9 no mo2-vfs: stat no mount dá ENOTCONN; `mo2-vfs --cleanup` desmonta e remove o estado
}
#[test] fn unknown_op_is_error_not_crash() {}
```

`group_alive` também tem teste de unidade em `watch.rs` (o próprio grupo do processo de teste está vivo; um pgid inexistente não).

- [ ] **Step 2:** `cargo test --test protocol` — FAIL.
- [ ] **Step 3: implementar** `ipc.rs`, `watch.rs` e o `main` (`tracing_appender::rolling::never` para o arquivo de `--log`; níveis mapeados de `--log-level`; stdout só recebe eventos, com `flush` após cada linha).
- [ ] **Step 4:** `cargo test` (todos) — PASS. `cargo clippy --all-targets -- -D warnings` limpo.
- [ ] **Step 5: Commit** — `mo2-vfs: line protocol, process group watching and cleanup mode`

---

### Task 9: Benchmarks do VFS

**Files:**
- Create: `mo2-vfs/src/bin/mo2-vfs-bench.rs`, `benchmarks/run-vfs.sh`, `benchmarks/results/2026-10-phase2-vfs.md`
- Modify: `benchmarks/gen_instance.py` só se precisar expor a lista de mods em ordem (ler o script antes)

**Interfaces:**
- Consumes: `session::Session`.
- Produces: `mo2-vfs-bench --data <dir> --mods <dir com N mods> [--files-sample 10000] --json` imprime `{ "mount_ms", "readdir_ms", "readdir_native_ms", "open_read_ms", "open_read_native_ms", "update_ms" }`. "Nativo" = a mesma árvore mesclada copiada para uma pasta comum (cópia feita pelo próprio bench antes de medir, fora do tempo).

- [ ] **Step 1:** Implementar o bench: monta via `Session::mount` com um mapping de diretório por mod (ordem alfabética = prioridade), mede `mount` até a volta da chamada; `readdir` recursivo completo (cache quente: medir a 2ª passada); `open`+`read`+`close` de uma amostra fixa (`--seed`) de 10.000 arquivos (2ª passada); `update` com a mesma lista.
- [ ] **Step 2:** `benchmarks/run-vfs.sh` (bash, `set -euo pipefail`, mesmo estilo de `benchmarks/run.sh`): gera a instância de 2.000 mods com `gen_instance.py` num tempdir, roda o bench 3 vezes, grava a mediana na tabela de `benchmarks/results/2026-10-phase2-vfs.md` (data, máquina via `uname -r` e `lscpu | grep 'Model name'`, tamanho real da instância, valores, metas da §8 e se foram atingidas).
- [ ] **Step 3:** Rodar no Bazzite (distrobox) e registrar. Meta não atingida → registrar a diferença e a causa provável no resultado (não bloqueia a tarefa; a Task 18 decide).
- [ ] **Step 4: Commit** — `bench: VFS mount, readdir and open benchmarks`

---

### Task 9b: Desempenho do VFS (inserida pelo controlador, Ruling 12)

A medição da Task 9 (`benchmarks/results/2026-10-phase2-vfs.md`) ficou 5–14× fora das metas da §8: `mount` 11,4 s, `update` 13,7 s, `readdir` 5,45×, `open`+`read` 6,27× (sem passthrough, em overlay). Esta tarefa ataca as causas medidas, sem mudar a semântica da §4.

**Files:**
- Modify: `mo2-vfs/src/tree.rs`, `mo2-vfs/src/layer.rs`, `mo2-vfs/src/fs.rs`, `mo2-vfs/src/inodes.rs`, `mo2-vfs/src/session.rs` (só o necessário), `mo2-vfs/Cargo.toml` (se precisar de `rayon`), `benchmarks/results/2026-10-phase2-vfs.md`

**Interfaces:**
- Consumes/Produces: nenhuma interface pública muda (`Tree::build_with`, `Session::*`, protocolo). Todos os testes existentes continuam passando sem alteração de comportamento.

- [ ] **Step 1: perfil.** Medir onde vai o tempo antes de mudar (instância menor, ex. `--mods 500 --files 50000`, num sistema de arquivos real — `BENCH_TMPDIR` em `$HOME/.cache`, não o overlay do container): `perf record -g` se disponível na distrobox (`sudo dnf install -y perf` é permitido), senão contadores de tempo temporários. Registrar no relatório as 3–5 maiores fontes de custo de `Tree::build`, `do_readdir`, `lookup`/`open`.
- [ ] **Step 2: construção da árvore.** Varredura das camadas em paralelo (uma tarefa por camada, ex. `rayon` ou `std::thread::scope` com um pool do tamanho de `available_parallelism`) produzindo listas por camada, e mesclagem sequencial em ordem de prioridade (o resultado tem de ser idêntico ao atual — teste de equivalência: árvore paralela == árvore sequencial para fixtures com conflitos, maiúsculas, whiteouts, skip). Reduzir alocações por entrada (dobra de nome uma vez por entrada, sem `PathBuf` intermediários desnecessários).
- [ ] **Step 3: `readdir`.** Sem custo quadrático: o offset vira índice direto (ex. `children` materializado num `Vec` por diretório no momento da listagem, ou cache de listagem por `fh` do `opendir`), e o inode de cada filho obtido sem `path_of` + dobra do caminho inteiro por entrada (ex. chave do filho = chave do pai + `/` + chave do nome) e com o lock do `Inodes` tomado uma vez por página, não por entrada.
- [ ] **Step 4: `open`/`read`.** Responder `open` com `FOPEN_KEEP_CACHE` (o page cache do kernel continua válido entre aberturas; a `update` e as escritas pelo VFS já invalidam o inode — conferir que `inval_inode` cobre os arquivos cuja origem mudou e que escrita pelo mount continua coerente) e manter TTLs de 60 s. Medir com passthrough desligado e ligado (`try_passthrough: true`, que cai para `pread` sem privilégio).
- [ ] **Step 5: `update`.** Mesmo caminho otimizado da construção; a comparação para invalidação (`changes()`) sem custo por entrada maior que O(1) amortizado.
- [ ] **Step 6: medir de novo** com `benchmarks/run-vfs.sh` na instância de 2.000 mods, temp dir num sistema de arquivos real (registrar o tipo), e acrescentar uma seção nova ao resultado com antes/depois. Metas da §8; o que não for atingido fica registrado com a causa medida (não bloqueia a tarefa).
- [ ] **Step 7:** `cargo test` (todos), fmt, clippy limpos; nenhum mount sobrando. **Commits** separados por otimização (`mo2-vfs: scan layers in parallel`, `mo2-vfs: linear readdir`, `mo2-vfs: keep the page cache across opens`, `bench: Phase 2 VFS after optimization`).

---

### Task 10: `LinuxVfsConnector` e o cliente do protocolo (fork)

**Files:**
- Create: `deps/modorganizer/src/linux/vfsclient_linux.h`, `deps/modorganizer/src/linux/vfsclient_linux.cpp`, `deps/modorganizer/tests/fusetest.h`
- Modify: `deps/modorganizer/src/vfsconnector.h`, `deps/modorganizer/src/linux/vfsconnector_linux.cpp`, `deps/modorganizer/src/CMakeLists.txt` (`vfsclient_linux.cpp` em `_organizer_launch_sources`), `deps/modorganizer/tests/test_vfsconnector.cpp`, `build.sh` (`pre_modorganizer` exporta `MO2_VFS_BINARY="$INSTALL/bin/mo2-vfs"` antes do `ctest`)

**Interfaces:**
- Consumes: protocolo da Task 8 (nomes de campo exatos: `op`, `mappings[].source|destination|is_directory|create_target`, `skip_file_suffixes`, `skip_directories`, `pgid`; eventos `hello|ready|ok|error|unmounted|status`).
- Produces (em `vfsconnector.h`, todos virtuais com corpo padrão na interface):

```cpp
  // mount points left by a previous run that couldn't be unmounted, for the user
  virtual QStringList staleMounts() const { return {}; }
  // mounts the virtual file system with the last mappings (or updates it if it is
  // already mounted); throws std::runtime_error with the reason when it can't
  virtual void activate() {}
  // the virtual file system stays mounted while this process group has processes
  virtual void watchProcessGroup(qint64 pgid) {}
  // unmounts if no process group is watched (the program didn't start)
  virtual void releaseIfIdle() {}
  virtual bool isActive() const { return false; }
  // true when inactive within the timeout
  virtual bool waitInactive(std::chrono::milliseconds timeout) { return true; }
  // called in the thread of the connector's object when the file system was unmounted
  virtual void setDeactivatedCallback(std::function<void()> f) {}
```

  - `class VfsClient` (`vfsclient_linux.h`): `VfsClient(QString binary, QStringList args)`; `QJsonObject request(const QJsonObject& r, std::chrono::milliseconds timeout)` (lança `std::runtime_error` em timeout, processo morto ou evento `error` — mensagem = `message` do evento, mais `path` se houver); `void setUnmountedHandler(std::function<void()>)` (eventos `unmounted` espontâneos); `bool running() const`; destrutor fecha o stdin e espera até 5 s (não mata: o processo pode estar segurando o mount de um jogo).
  - `class LinuxVfsConnector : public VfsConnector` substitui `NullVfsConnector` (removido, e os testes dele adaptados); `createVfsConnector()` roda `mo2-vfs --cleanup` uma vez (síncrono, 10 s; raízes não desmontadas → `log::warn` + devolvidas por `staleMounts()`) e retorna o connector. `updateForcedLibraries` continua só registrando no log, como no `NullVfsConnector`. Se o processo `mo2-vfs` terminar com o VFS ativo: `log::error` com o código de saída e o fim do stderr, `isActive()` passa a `false`, o callback de desativação é chamado e o próximo `activate()` inicia um processo novo.
  - `tests/fusetest.h`: `bool requireFuse()` (mesma regra do Rust, com `GTEST_SKIP()` no chamador) e `QString vfsBinary()` (lê `MO2_VFS_BINARY`).

- [ ] **Step 1: testes** (`test_vfsconnector.cpp`; diretórios com `TempDir`; `MO2_VFS_STATE_DIR` definido num `TempDir` no `SetUp`)

```cpp
TEST(VfsConnector, LinuxName) { EXPECT_EQ(createVfsConnector()->name(), "mo2-vfs"); }

TEST(VfsConnector, UpdateMappingDoesNotMount)
{
  if (!requireFuse()) GTEST_SKIP();
  // updateMapping com um mod -> isActive() == false e o destino não está em /proc/self/mountinfo
}

TEST(VfsConnector, ActivateMountsAndMergesMods)
{
  if (!requireFuse()) GTEST_SKIP();
  // data/ com original.esm, mods A e B, overwrite (createTarget) -> activate():
  // isActive(), data/ lista original.esm + arquivos de A e B; arquivo criado em data/ aparece em overwrite/
}

TEST(VfsConnector, BackslashPathsWork)  // mappings com '\' (como NullMappingPathsAreDiskPaths) montam certo

TEST(VfsConnector, ReleaseIfIdleUnmounts)

TEST(VfsConnector, AutoDeactivatesWhenGroupEnds)
{
  if (!requireFuse()) GTEST_SKIP();
  // activate; inicia `sleep 1` num grupo próprio (posix_spawn com POSIX_SPAWN_SETPGROUP);
  // watchProcessGroup(pid); waitInactive(5 s) == true; callback chamado uma vez (QEventLoop processado)
}

TEST(VfsConnector, ActivateErrorIsException)
{
  // MO2_VFS_BINARY apontando para um script que imprime {"event":"hello"} e responde
  // {"event":"error","code":"mount_busy","message":"busy","path":"/x"} -> activate() lança runtime_error contendo "busy" e "/x"
}

TEST(VfsConnector, MissingBinaryIsException)  // MO2_VFS_BINARY inexistente -> activate() lança, mensagem cita o caminho

TEST(VfsConnector, HelperCrashDeactivates)
{
  if (!requireFuse()) GTEST_SKIP();
  // activate; kill -9 no processo mo2-vfs (pid lido de /proc pelo nome, filho deste processo) ->
  // isActive() == false em até 2 s, callback chamado; activate() de novo funciona (após mo2-vfs --cleanup implícito)
}

TEST(VfsConnector, StaleMountsReported)
{
  // MO2_VFS_BINARY para um script que, com --cleanup, imprime {"root":"/x","ok":false} e sai com 1 -> staleMounts() == {"/x"}
}

TEST(VfsConnector, ParamsSkipListsAreSent)
{
  if (!requireFuse()) GTEST_SKIP();
  // updateParams(..., skipFileSuffixes {".mohidden"}, skipDirectories {"fomod"}); activate -> a.esp.mohidden e fomod/ não aparecem
}
```

- [ ] **Step 2:** `./build.sh --only mo2-vfs --only modorganizer --tests` — FAIL nos testes novos.
- [ ] **Step 3: implementar** `VfsClient` (`QProcess` com `setProcessChannelMode(SeparateChannels)`, stderr repassado ao log do MO2 em `debug`; leitura por linha em `readyReadStandardOutput`; `request` usa um `QEventLoop` com `QTimer` de timeout; espera o `hello` na partida) e `LinuxVfsConnector` (estado: últimos mappings normalizados, `skip*`, nível de log, `std::unique_ptr<VfsClient>`, `bool m_active`, `int m_watched`). Argumentos: `--log <dataPath>/<AppConfig::logPath()>/mo2-vfs.log --log-level <nível>`; sem `dataPath` (testes), sem `--log`. No Windows nada muda (`usvfsconnector.*` intocado).
- [ ] **Step 4:** testes — PASS. Rodar também o lint do fork (`clang-format --dry-run -Werror` nos arquivos tocados).
- [ ] **Step 5: Commit** (fork) — `Linux: mo2-vfs connector`; (superprojeto) — `build: pass mo2-vfs to the application tests`

---

### Task 11: Executar programas Linux com o VFS montado (fork)

**Files:**
- Modify: `deps/modorganizer/src/linux/spawn_linux.cpp`, `deps/modorganizer/src/linux/processrunner_linux.cpp`, `deps/modorganizer/src/organizercore.cpp` (`beforeRun`, `afterRun`, `refreshDirectoryStructure`, construtor), `deps/modorganizer/src/organizercore.h` (membro `bool m_RefreshPendingAfterVfs = false;`), `deps/modorganizer/tests/test_launch.cpp`
- Test: `deps/modorganizer/tests/test_launch.cpp`

**Interfaces:**
- Consumes: métodos novos de `VfsConnector` (Task 10).
- Produces:
  - `spawn::isNativeProgram(const QFileInfo&) -> bool` (declarado em `spawn.h` dentro de `#ifndef _WIN32`): arquivo regular com permissão de execução e (cabeçalho `\x7fELF` ou começa com `#!`).
  - `spawn::startBinary` no Linux: programa nativo → `posix_spawn` com `POSIX_SPAWN_SETPGROUP` (pgroup 0), `chdir` para `sp.currentDirectory` (via `posix_spawn_file_actions_addchdir_np`), argumentos de `sp.arguments` divididos com `QProcess::splitCommand`, ambiente do MO2; retorna o pid como `HANDLE`. Qualquer outro alvo → mensagem da Fase 3 (como hoje).
  - `ProcessRunner::run` no Linux: documento (`FileExecutionTypes::Other`) continua abrindo no aplicativo; programa nativo segue o fluxo da §6.2; o resto mantém a mensagem da Fase 3.

- [ ] **Step 1: testes** (`test_launch.cpp`)

```cpp
TEST(Launch, NativeProgramDetection)
{
  TempDir tmp;
  // script "#!/bin/sh" executável -> true; mesmo script sem +x -> false;
  // cópia de /bin/true -> true; "a.exe" com conteúdo MZ e +x -> false; diretório -> false
}

TEST(Launch, StartNativeInOwnProcessGroup)
{
  // startBinary com um script que grava $(pwd) e "$1" num arquivo, arguments "\"um arg\"", currentDirectory = tmp:
  // processo termina; arquivo contém tmp e "um arg"; getpgid(pid) == pid (lido antes do fim via script que dorme 0.5 s)
}

TEST(Launch, ExeStillShowsPhase3Message)  // isNativeProgram("SkyrimSE.exe") == false; UnavailableMessage inalterado
```

O fluxo completo (montar → executar → desmontar) é coberto pela Task 13.

- [ ] **Step 2:** testes — FAIL.
- [ ] **Step 3: implementar**:
  - `OrganizerCore::beforeRun`: depois de `m_VFS->updateMapping(...)`, `m_VFS->activate();` no mesmo `try` (o `catch (const std::exception&)` existente mostra a mensagem e cancela).
  - `ProcessRunner::run` (Linux): para programa nativo, reproduzir a sequência do `run()` Windows (`processrunner.cpp`, bloco `_WIN32`) que salva o perfil e chama `m_core.beforeRun`, iniciar com `spawn::startBinary`; se falhar, `m_core.vfs().releaseIfIdle()` (adicionar `VfsConnector& OrganizerCore::vfs()`), senão `watchProcessGroup(pid)` e seguir para `postRun()`.
  - `OrganizerCore::afterRun`: no início, `#ifndef _WIN32` `if (!m_VFS->waitInactive(std::chrono::seconds(5))) log::warn(...)`.
  - `OrganizerCore::refreshDirectoryStructure`: no início, `if (m_VFS->isActive()) { m_RefreshPendingAfterVfs = true; log::debug("directory refresh deferred until the virtual file system is unmounted"); return; }`. No construtor, `m_VFS->setDeactivatedCallback([this] { if (std::exchange(m_RefreshPendingAfterVfs, false)) refreshDirectoryStructure(); });`.
  - Mounts antigos: quando a interface fica disponível (`OrganizerCore::setUserInterface`), se `m_VFS->staleMounts()` não estiver vazio, mostrar uma vez um `QMessageBox::warning` listando os caminhos e o comando `fusermount3 -uz <caminho>`.
  - Atualizar os comentários de cabeçalho de `spawn_linux.cpp` e `processrunner_linux.cpp` (o que mudou nesta fase).
- [ ] **Step 4:** testes — PASS; build completo do app (`./build.sh --only mo2-vfs --only modorganizer --tests`).
- [ ] **Step 5: Commit** (fork) — `Linux: run native programs with the virtual file system`

---

### Task 12: `fileMapping()` testável (pendência da Fase 1)

**Files:**
- Create: `deps/modorganizer/src/filemappingbuilder.h`, `deps/modorganizer/src/filemappingbuilder.cpp`, `deps/modorganizer/tests/test_filemapping.cpp`
- Modify: `deps/modorganizer/src/organizercore.cpp` (`fileMapping(profile, customOverwrite)` delega), `deps/modorganizer/src/CMakeLists.txt` (fonte nova em `organizer_launch` e no app, nas duas plataformas)

**Interfaces:**
- Produces:

```cpp
// the part of OrganizerCore::fileMapping() that doesn't need the application: which
// mod directories are mapped where, in priority order
struct ActiveModDir { QString name; QString path; bool regular; };
MappingType buildModMappings(const std::vector<ActiveModDir>& activeMods /* priority order */,
                             const QMap<QString, QStringList>& dataMaps,
                             const QString& overwritePath, const QString& customOverwrite);
// throws MyException when customOverwrite is not empty and not an active mod
```

- [ ] **Step 1: testes** (`test_filemapping.cpp`, pastas reais em `TempDir`, porque o código confere `QDir::exists`)

```cpp
TEST(FileMapping, ModsInPriorityOrderThenOverwrite)        // [A, B, overwrite], só overwrite com createTarget
TEST(FileMapping, SkipsOverwriteEntryAndNonRegularMods)     // "overwrite" na lista e separadores (regular=false) não geram mapping
TEST(FileMapping, DataMapSubdirectoriesAndSecondaryDirs)    // dataMaps {"": [Data, Data2], "Root": [Game]} -> mod/Root mapeado para Game, raiz para Data e Data2
TEST(FileMapping, CustomOverwriteIsCreateTarget)            // customOverwrite = "B": B com createTarget, overwrite sem
TEST(FileMapping, CustomOverwriteNotActiveThrows)
TEST(FileMapping, MissingModSubdirIsNotMapped)              // mod sem pasta "Root" -> nenhum mapping para Game
```

- [ ] **Step 2:** FAIL. **Step 3:** extrair o laço sobre `profile.getActiveMods()` e o bloco do overwrite para `buildModMappings` sem mudar o comportamento (saves locais e `IPluginFileMapper` continuam em `OrganizerCore::fileMapping`). **Step 4:** PASS; build Windows não é afetado (fonte nova compilada nas duas plataformas, sem API Linux).
- [ ] **Step 5: Commit** (fork) — `Extract the mod part of fileMapping() and test it`

---

### Task 13: Teste ponta a ponta e CI

**Files:**
- Create: `tests/vfs-e2e/run.sh`, `tests/vfs-e2e/probe.sh`, `tests/fixtures/vfs-e2e/` (instância portátil mínima: `ModOrganizer.ini` com `gameName=Skyrim Special Edition` e `gamePath` apontando para uma pasta de jogo falsa criada pelo `run.sh`, `profiles/Default/modlist.txt` com `+ModA`/`+ModB`, `mods/ModA`, `mods/ModB`, `overwrite/`)
- Modify: `build.sh` (com `--tests`, roda `tests/vfs-e2e/run.sh "$INSTALL"` depois do smoke test), `.github/workflows/linux.yml` (nada além da Task 1, se o `build.sh --tests` já roda)

**Interfaces:**
- Consumes: app completo instalado, `mo2-vfs` instalado; executável configurado na instância apontando para `probe.sh`.
- Produces: `tests/vfs-e2e/run.sh <install>` sai com 0 quando: o `probe.sh` (rodando com o VFS montado) listou `Data/` com o original e os arquivos de A e B (B vencendo em conflito), leu o conteúdo de B, e criou `Data/probe-created.txt`; depois do fim, `overwrite/probe-created.txt` existe, a pasta `Data` real do jogo falso tem exatamente os arquivos originais, e nada está montado (`findmnt -t fuse.mo2-vfs` vazio).

- [ ] **Step 1:** Ler `tests/smoke/modorganizer-smoke.sh` e o tratamento de `moshortcut://`/`-e`/`run` na linha de comando do app (`src/commandline.cpp`) para escolher o jeito de executar um programa da instância sem interface (preferência: `ModOrganizer -i <instância> run <probe.sh>` com `QT_QPA_PLATFORM=offscreen`, `--disable-gpu`, `HOME` e `TMPDIR` privados). O jogo falso precisa ser aceito pelo `game_bethesda` (criar `SkyrimSE.exe` vazio e `Data/Skyrim.esm`).
- [ ] **Step 2:** Escrever `probe.sh` (grava o resultado das verificações em `$PROBE_OUT`) e `run.sh` (monta a instância num tempdir a partir do fixture, roda, confere, limpa; em falha imprime `logs/mo2-vfs.log` e o log do MO2).
- [ ] **Step 3:** `./build.sh --tests` na distrobox — PASS. Push e conferir no CI Linux.
- [ ] **Step 4: Commit** — `tests: end-to-end run of a native program through mo2-vfs`

---

### Task 14: Varredura de pastas — `EINTR`, `fstat` da raiz, links simbólicos (pendência)

**Files:**
- Modify: `deps/modorganizer/src/linux/envfs_linux.cpp`, `deps/modorganizer/tests/test_envfs.cpp`

- [ ] **Step 1: testes** (`test_envfs.cpp`)

```cpp
TEST(EnvFs, RootThatIsNotADirectoryIsReported)   // forEachEntry num arquivo comum: nenhum callback, aviso, sem crash
TEST(EnvFs, SymlinkToDirectoryIsNotListedTwice)  // mod/a/x.txt e mod/link -> a: x.txt aparece uma vez por caminho, e link/ não reentra em a/ (sem duplicar entradas)
TEST(EnvFs, SymlinkLoopTerminates)               // mod/loop -> . : varredura termina
TEST(EnvFs, InterruptedReadIsRetried)            // getdents64/fstatat com EINTR: injetar via um alarme (setitimer 1 ms + handler vazio sem SA_RESTART) durante a varredura de 20.000 arquivos; contagem final correta
```

- [ ] **Step 2:** FAIL. **Step 3:** repetir `getdents64`/`fstatat`/`openat` em `EINTR`; checar o resultado do `fstat` da raiz (linha ~144) e de `fstat(fd, &dst)` (~238); para links simbólicos para diretório, guardar `(st_dev, st_ino)` dos diretórios já abertos na pilha e não entrar de novo. **Step 4:** PASS. **Step 5: Commit** (fork) — `Linux: retry EINTR and follow directory links once in the directory walker`

---

### Task 15: Consolidar duplicações (pendência)

**Files:**
- Create: `deps/uibase/include/uibase/linux/filetime.h`, `deps/uibase/include/uibase/linux/inifile.h`, `deps/uibase/src/linux/inifile_linux.cpp`, testes em `deps/uibase/tests/`
- Modify: `deps/modorganizer/src/linux/envfs_linux.cpp`, `deps/modorganizer/src/linux/pluginlist_linux.cpp`, `deps/modorganizer/src/linux/win32compat_linux.cpp`, `deps/game_bethesda/src/gamebryo/linux/win32compat_linux.cpp`, `deps/uibase/CMakeLists.txt` ou `src/CMakeLists.txt`

**Interfaces:**
- Produces (Linux-only, aditivo no `uibase`): `MOBase::linux::toFileTime(const timespec&) -> FILETIME`, `MOBase::linux::toTimespec(FILETIME) -> timespec`, `MOBase::linux::fileTimeTicks(FILETIME) -> std::uint64_t` (header-only); `MOBase::linux::IniFile` com o comportamento atual de `GetPrivateProfileString`/`WritePrivateProfileString` das duas cópias (ler as duas antes e unir os casos que cada uma trata).

- [ ] **Step 1:** Inventário: `grep -rn "11644473600\|116444736000000000\|10000000" deps/modorganizer/src/linux deps/*/src` e `grep -rn "PrivateProfile" deps/modorganizer/src/linux deps/*/src/**/linux`. Registrar no commit as cópias encontradas. A cópia em `deps/archive/src/linux/fileio_linux.cpp` **fica** (o `archive` não depende do `uibase`), com um comentário apontando para `uibase/linux/filetime.h`.
- [ ] **Step 2: testes** no `uibase`: ida e volta `timespec`↔`FILETIME` (epoch Unix = `116444736000000000`; nanossegundos truncados a 100 ns; datas antes de 1970); `IniFile`: chave existente, ausente com padrão, seção ausente, aspas, comentários `;` e `#`, CRLF, chave repetida (vale a primeira, como hoje — o comportamento do Windows fica como pendência da Fase 4).
- [ ] **Step 3:** FAIL → implementar → PASS; trocar as cópias do fork e do `game_bethesda` pelos novos headers. Os testes existentes de `pluginlist_linux`, `envfs` e `game_bethesda` continuam passando.
- [ ] **Step 4:** "Mecanismos de include do `uibase`": ler como o fork e os plugins incluem `win32compat.h` e os headers Linux do `uibase` (`grep -rn "win32compat\|uibase/linux" deps/*/src | sort | uniq -c`); deixar um só jeito (`#include <uibase/win32compat.h>` vindo do include do `uibase`) e remover os caminhos alternativos. Se a mudança for maior que ~20 arquivos, parar e registrar como pendência da Fase 3 em vez de fazer.
- [ ] **Step 5: Commits** — `deps/uibase: Linux FILETIME and INI helpers`, (fork) `Linux: use the uibase FILETIME and INI helpers`, `deps/game_bethesda: use the uibase INI helper`

---

### Task 16: Guard no CI contra leituras de 64 bits nos saves (pendência)

**Files:**
- Create: `scripts/check-save-reads.sh`
- Modify: `.github/workflows/linux.yml` (passo antes do build), `deps/game_bethesda/src/gamebryo/gamebryosavegame.h` (se a causa estiver no template)

- [ ] **Step 1:** Ler `FileWrapper::read`/`skip` em `gamebryosavegame.h` (linha ~106) e descobrir por que `read<int64_t>`/`skip<uint64_t>` leem 4 bytes no Linux (provável: sobrecarga ou especialização para `unsigned long`/`long`, que tem 4 bytes no Windows e 8 no Linux).
- [ ] **Step 2:** Se a causa for o template, corrigir com tipos de tamanho fixo e acrescentar `static_assert(sizeof(T) == …)` onde o tamanho no arquivo é fixo; teste em `deps/game_bethesda` que lê um buffer com um `uint64_t` conhecido e confere o valor e a posição seguinte.
- [ ] **Step 3:** `check-save-reads.sh`: falha (exit 1, imprimindo arquivo:linha) se encontrar `read<\s*(std::)?u?int64_t\s*>` ou `skip<\s*(std::)?u?int64_t\s*>` ou `read<\s*(unsigned\s+)?long\s*>` em `deps/game_bethesda/src`, exceto linhas com o comentário `// save-read-ok: <motivo>`. Rodar no CI.
- [ ] **Step 4:** CI verde. **Step 5: Commit** — `ci: guard against platform-sized reads in save games` (+ `deps/game_bethesda:` se houve correção)

---

### Task 17: Smoke test até a janela principal (pendência)

**Files:**
- Modify: `tests/smoke/modorganizer-smoke.sh`, `.github/workflows/linux.yml`
- Create: `tests/fixtures/smoke-instance/` (reaproveitar a instância do fixture da Task 13 se servir)

- [ ] **Step 1:** Estender o smoke test: com uma instância pronta (portátil, jogo falso da Task 13), `QT_QPA_PLATFORM=offscreen`, `--disable-gpu`, `HOME` e `TMPDIR` privados (tempdir removido no fim), o app abre até a janela principal (critério: a linha de log que o app grava ao mostrar a janela principal — procurar em `src/mainwindow.cpp`/`src/main.cpp` e citar a linha no script) e é encerrado com `SIGTERM` em até 30 s, sem crash dump.
- [ ] **Step 2:** Hermético: nenhuma escrita fora dos tempdirs (conferir com `find "$HOME_ORIG" -newer <marcador>` vazio no CI).
- [ ] **Step 3:** CI roda o smoke **uma vez**: remover a chamada duplicada (hoje ele roda dentro do `build.sh --tests` e de novo num passo próprio do workflow — manter só a do `build.sh`).
- [ ] **Step 4:** CI verde. **Step 5: Commit** — `tests: smoke test up to the main window, hermetic, once per CI run`

---

### Task 18: Documentação, checklist manual e fechamento

**Files:**
- Create: `docs/testing/phase2-manual-checklist.md`
- Modify: `ROADMAP.md`, `STATE.md`, `README.md`, `docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md` (§1.2 e §3.1), `deps/modorganizer/src/linux/README.md`

- [ ] **Step 1:** Spec geral: §1.2/§3.1 — VFS em processo auxiliar com JSON por stdin/stdout (link para a spec da Fase 2), resultado do spike; §3.2 — risco do `pressure-vessel` resolvido; novo risco da Fase 3: processos do Wine que saem do grupo de processos (`setsid`/`wineserver`) e o cache de diretórios do Wine.
- [ ] **Step 2:** `ROADMAP.md`: Fase 2 com status, bullets atualizados (sem Corrosion/`cbindgen`), link para spec e plano. `STATE.md`: tabela de tarefas da Fase 2, critérios de pronto da §10 com status, decisões novas (2026-10-15: processo auxiliar; proteção dos originais; execução nativa na Fase 2; refresh adiado enquanto montado), pendências que sobraram, resultado dos benchmarks.
- [ ] **Step 3:** `docs/testing/phase2-manual-checklist.md`, no formato do checklist da Fase 1: no Bazzite, criar instância Skyrim SE, ativar 2 mods com conflito, cadastrar `/usr/bin/dolphin` (ou `nautilus`) como executável, Executar → navegar em `Data/` (arquivos dos mods, conflito resolvido, pasta do jogo intacta depois), criar um arquivo → aparece em `overwrite/` após fechar; fechar o MO2 com o gerenciador aberto → `Data` continua montada até fechar o gerenciador; matar o `mo2-vfs` com `kill -9` → reabrir o MO2 limpa o mount; `findmnt -t fuse.mo2-vfs` vazio no fim.
- [ ] **Step 4:** Revisão final do branch (superpowers:requesting-code-review), correções, checkpoint de CI Linux e Windows (commit com `[windows-ci]`).
- [ ] **Step 5: Commit** — `docs: Phase 2 state, roadmap and manual checklist`
