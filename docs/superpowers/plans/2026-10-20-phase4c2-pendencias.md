# Fase 4c.2 — Pendências da Fase 2 e INIs — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fechar as pendências adiadas da Fase 2 (P1–P5, P7), os follow-ups de INI da 4c.1 (I1, I2) e as vizinhas R1 (reconferir a tabela de mounts) e R2 (limitar `MO2_VFS_TIMEOUT_MS`).

**Architecture:** O conector Linux do VFS (`vfsconnector_linux.cpp`) passa a tratar o helper travado com programa observado como "deixado montado" e reconfere a tabela de mounts por timer, de forma assíncrona, até as raízes sumirem; os timeouts vão para um arquivo pequeno testável. O `main.cpp` confere a tela antes de criar a `QApplication`, por uma função pura. No `game_bethesda`, os pontos centrais de leitura e escrita dos INIs de arquivos e da invalidação de BSA resolvem o nome ignorando maiúsculas. O resto é documentação.

**Tech Stack:** C++20/Qt 6 (`QProcess`, `QTimer`, `QProcessEnvironment`), gtest (`organizer_tests`, `game_gamebryo_tests`), bash (smoke), Markdown.

**Spec:** `docs/superpowers/specs/2026-10-20-phase4c2-pendencias-design.md` (ler inteira antes de cada tarefa; §N são seções dela).

## Global Constraints

- Worktree `../ModOrganizerLinux-4c2` (`/var/home/fmoura-server/Documentos/Applications/ModOrganizerLinux-4c2`), branch `fase-4c2-pendencias` (a partir de `fase-4c1-plugins`; a spec já está nele). **Nunca** compilar, instalar ou rodar testes no checkout principal (`../ModOrganizerLinux`): o MO2 do usuário roda do `install/` de lá.
- Commits: mudanças dentro de `deps/modorganizer` (código e `tests/` dele) em commits `deps/modorganizer: Linux: …`; dentro de `deps/game_bethesda` em `deps/game_bethesda: Linux: …`; `tests/` do superprojeto em `tests: …`; documentação em `docs: …`. Sem misturar arquivos de fora do subtree num commit `deps/…`. Todo commit termina com as linhas de atribuição da sessão:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL
  ```
- Mensagens ao usuário e de log em inglês; comentários no estilo do arquivo (frases em minúsculas, explicando o porquê); `clang-format` de `deps/modorganizer/.clang-format` só nas linhas alteradas.
- Código Linux-only fica em arquivos `linux/*_linux.cpp` ou em `#ifndef _WIN32`. No `game_bethesda`, a chamada a `GameGamebryo::resolveIgnoringCase()` vale nas duas plataformas (identidade no Windows), como na 4c.1 (spec §1.2).
- Valores fixos: reconferência a cada **5 s** (`MO2_VFS_RECHECK_MS` só encurta); timeout de `mount`/`update` **30 s** (`MO2_VFS_TIMEOUT_MS` só encurta); prazo de cada processo da reconferência **10 s**.
- Texto exato da mensagem sem tela: `Mod Organizer needs a graphical session, but neither WAYLAND_DISPLAY nor DISPLAY is set. Run it from the desktop, or set WAYLAND_DISPLAY, XDG_RUNTIME_DIR, DISPLAY and XAUTHORITY to those of the desktop session.`
- Arquivos `*_en.ts` regenerados pelo build não são commitados: `git checkout -- '*_en.ts'` antes de cada commit se aparecerem no `git status`.

## Ambiente de execução

O CI (GitHub Actions, container Fedora 44 de `build/Containerfile`) é a referência. No Bazzite, compile na distrobox `mo2-run`, a partir da raiz do worktree:

```bash
cd /var/home/fmoura-server/Documentos/Applications/ModOrganizerLinux-4c2
mkdir -p /tmp/mo2t
distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only modorganizer --tests
```

O `TMPDIR` curto e próprio evita a memória compartilhada de instância única que o MO2 aberto do usuário mantém em `/tmp`. Um teste só, depois do primeiro build: `distrobox enter mo2-run -- bash -c 'cd build-out/modorganizer && cmake --build . --target organizer_tests && TMPDIR=/tmp/mo2t ctest --output-on-failure -R "<regex>"'`. Para o `game_bethesda`: `./build.sh --only game_bethesda --tests` (depois de `modorganizer` instalado no `install/` do worktree; o primeiro build completo do worktree é `./build.sh --tests`, longo: rode em segundo plano). Nunca `pkill -f`/`pgrep -f` com padrão que aparece na própria linha de comando.

## Review Focus

1. **Reconferência que nunca para** (timer ligado depois de as raízes saírem por outro caminho: `unmounted` do helper, `doActivate()` que monta de novo): `deactivated()` e o caminho de sucesso de `doActivate()` desligam o timer; `recheckLeftMounts()` volta cedo sem raízes → Task 2 (asserção de que o processo de reconferência não roda depois do callback) e Task 3.
2. **Destruição do conector com um `--list-mounts` em andamento** (fechar o MO2 durante a reconferência): o destrutor desconecta, mata e apaga o processo; nenhum callback roda sobre um conector destruído → Task 2, teste `DestroyedWhileRecheckingDoesNotCrash`.
3. **Helper travado morto com o jogo ainda usando o mount**: só morre depois de a tabela não listar mais as raízes; com a raiz listada, `activate()` falha e o pid continua vivo → Task 3.
4. **`-platform offscreen` dado na linha de comando com `QT_QPA_PLATFORM` vazio e sem tela** (testes, CI, quem roda sem tela de propósito): passa → Task 4.
5. **INI que não existe em nenhuma caixa** (perfil novo): é criado com o nome pedido, como antes → Task 5.

---

### Task 1: Timeouts do VFS testáveis (R2)

**Files:**
- Create: `deps/modorganizer/src/linux/vfstimeouts_linux.h`
- Create: `deps/modorganizer/src/linux/vfstimeouts_linux.cpp`
- Modify: `deps/modorganizer/src/CMakeLists.txt` (`_organizer_launch_sources`, ~linha 130)
- Modify: `deps/modorganizer/src/linux/vfsconnector_linux.cpp` (`mountTimeout()`, linhas 111–117, e suas 3 chamadas)
- Test: `deps/modorganizer/tests/test_vfsconnector.cpp`

**Interfaces:**
- Produces: `std::chrono::milliseconds vfsMountTimeout();` e `std::chrono::milliseconds vfsRecheckInterval();` (em `linux/vfstimeouts_linux.h`, namespace global).

- [ ] **Step 1: Teste que falha**

Em `deps/modorganizer/tests/test_vfsconnector.cpp`, depois de `#include "vfsconnector.h"`, acrescente `#include "linux/vfstimeouts_linux.h"`. No fim do arquivo (depois de `TEST(VfsConnector, NoUsvfsOnLinux)`), acrescente:

```cpp
// the variables of the tests can only shorten the timeouts; an empty, zero, negative or
// invalid value is the default
TEST(VfsTimeouts, VariablesOnlyShortenThem)
{
  using namespace std::chrono_literals;

  const auto oldTimeout = qgetenv("MO2_VFS_TIMEOUT_MS");
  const auto oldRecheck = qgetenv("MO2_VFS_RECHECK_MS");

  qunsetenv("MO2_VFS_TIMEOUT_MS");
  qunsetenv("MO2_VFS_RECHECK_MS");
  EXPECT_EQ(vfsMountTimeout(), 30s);
  EXPECT_EQ(vfsRecheckInterval(), 5s);

  qputenv("MO2_VFS_TIMEOUT_MS", "200");
  qputenv("MO2_VFS_RECHECK_MS", "100");
  EXPECT_EQ(vfsMountTimeout(), 200ms);
  EXPECT_EQ(vfsRecheckInterval(), 100ms);

  qputenv("MO2_VFS_TIMEOUT_MS", "999999");
  qputenv("MO2_VFS_RECHECK_MS", "999999");
  EXPECT_EQ(vfsMountTimeout(), 30s);
  EXPECT_EQ(vfsRecheckInterval(), 5s);

  for (const char* bad : {"0", "-5", "abc"}) {
    qputenv("MO2_VFS_TIMEOUT_MS", bad);
    qputenv("MO2_VFS_RECHECK_MS", bad);
    EXPECT_EQ(vfsMountTimeout(), 30s) << bad;
    EXPECT_EQ(vfsRecheckInterval(), 5s) << bad;
  }

  for (const auto& [name, old] : {std::pair{"MO2_VFS_TIMEOUT_MS", oldTimeout},
                                  std::pair{"MO2_VFS_RECHECK_MS", oldRecheck}}) {
    if (old.isNull()) {
      qunsetenv(name);
    } else {
      qputenv(name, old);
    }
  }
}
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only modorganizer --tests`
Expected: erro de compilação (`linux/vfstimeouts_linux.h` não existe).

- [ ] **Step 3: Implementar**

`deps/modorganizer/src/linux/vfstimeouts_linux.h`:

```cpp
#ifndef VFSTIMEOUTS_LINUX_H
#define VFSTIMEOUTS_LINUX_H

#include <chrono>

// the timeout of the mount and update requests of mo2-vfs: 30 s; MO2_VFS_TIMEOUT_MS
// (tests) can only shorten it
std::chrono::milliseconds vfsMountTimeout();

// how often a mount that could not be unmounted is checked again: 5 s;
// MO2_VFS_RECHECK_MS (tests) can only shorten it
std::chrono::milliseconds vfsRecheckInterval();

#endif  // VFSTIMEOUTS_LINUX_H
```

`deps/modorganizer/src/linux/vfstimeouts_linux.cpp`:

```cpp
#include "vfstimeouts_linux.h"

#include <QtGlobal>
#include <algorithm>

namespace
{
// the value of the variable `name` when it is a positive number of milliseconds below
// `max`; `max` otherwise
std::chrono::milliseconds shortenedBy(const char* name, std::chrono::milliseconds max)
{
  bool ok             = false;
  const auto variable = qEnvironmentVariable(name).toLongLong(&ok);
  return ok && variable > 0 ? std::min(std::chrono::milliseconds(variable), max) : max;
}
}  // namespace

std::chrono::milliseconds vfsMountTimeout()
{
  return shortenedBy("MO2_VFS_TIMEOUT_MS", std::chrono::seconds(30));
}

std::chrono::milliseconds vfsRecheckInterval()
{
  return shortenedBy("MO2_VFS_RECHECK_MS", std::chrono::seconds(5));
}
```

Em `deps/modorganizer/src/CMakeLists.txt`, em `_organizer_launch_sources`, depois de `linux/vfsconnector_linux.cpp` (a última linha, que fecha com `)`), troque `linux/vfsconnector_linux.cpp)` por:

```cmake
	linux/vfsconnector_linux.cpp
	linux/vfstimeouts_linux.cpp)
```

Em `vfsconnector_linux.cpp`: acrescente `#include "vfstimeouts_linux.h"` depois de `#include "vfsclient_linux.h"`; apague a função `mountTimeout()` inteira (o comentário `// the timeout of mount and update requests; MO2_VFS_TIMEOUT_MS replaces it (tests)` e as 6 linhas da função); troque as três chamadas `mountTimeout()` por `vfsMountTimeout()`.

- [ ] **Step 4: Rodar e ver passar**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only modorganizer --tests`
Expected: `organizer_tests` passa, incluindo `VfsTimeouts.VariablesOnlyShortenThem` e os testes do conector que usam `MO2_VFS_TIMEOUT_MS=300`.

- [ ] **Step 5: Commit**

```bash
git add deps/modorganizer/src/linux/vfstimeouts_linux.h deps/modorganizer/src/linux/vfstimeouts_linux.cpp \
  deps/modorganizer/src/CMakeLists.txt deps/modorganizer/src/linux/vfsconnector_linux.cpp \
  deps/modorganizer/tests/test_vfsconnector.cpp
git commit -m "deps/modorganizer: Linux: MO2_VFS_TIMEOUT_MS only shortens the mount timeout

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL"
```

---

### Task 2: Reconferência assíncrona das raízes deixadas montadas (R1)

**Files:**
- Modify: `deps/modorganizer/src/linux/vfsconnector_linux.cpp`
- Test: `deps/modorganizer/tests/test_vfsconnector.cpp`

**Interfaces:**
- Consumes: `vfsRecheckInterval()` (Task 1).
- Produces (privados de `LinuxVfsConnector`, usados pela Task 3): `void recheckLeftMounts();`, `void runHelperAsync(const QStringList& args, std::function<void(bool, const QByteArray&)> done);`, membros `QTimer m_recheck;` e `QProcess* m_recheckProcess = nullptr;`; função livre (namespace anônimo) `std::optional<QStringList> parseMountedRoots(const QByteArray& output);`. Comportamento: `keepLeftMounted()` liga o timer; `deactivated()` o desliga.

- [ ] **Step 1: Testes que falham**

Em `test_vfsconnector.cpp`:

1. No namespace anônimo, depois de `readFile()`, acrescente:

```cpp
void writeFile(const fs::path& p, const std::string& content)
{
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << content;
}
```

2. No fixture `VfsConnectorTest`, guarde e restaure também `MO2_VFS_RECHECK_MS` e `MO2_VFS_TIMEOUT_MS`: em `SetUp()` acrescente `m_oldRecheck = qgetenv("MO2_VFS_RECHECK_MS");` e `m_oldTimeout = qgetenv("MO2_VFS_TIMEOUT_MS");`; em `TearDown()` acrescente `restore("MO2_VFS_RECHECK_MS", m_oldRecheck);` e `restore("MO2_VFS_TIMEOUT_MS", m_oldTimeout);`; troque a declaração `QByteArray m_oldState, m_oldBinary, m_oldBridge;` por `QByteArray m_oldState, m_oldBinary, m_oldBridge, m_oldRecheck, m_oldTimeout;`.

3. Em `TEST_F(VfsConnectorTest, UnreadableMountTableKeepsTheMount)`, logo depois do `fakeHelper(...)`, acrescente `qputenv("MO2_VFS_RECHECK_MS", "100");`, e depois de `EXPECT_TRUE(vfs->staleMounts().contains("/x/Data"));` acrescente:

```cpp
  // rechecked a few times: the table still can't be read
  processEvents(500);
  EXPECT_TRUE(vfs->isActive());
  EXPECT_EQ(called, 0);
```

4. Depois de `UnreadableMountTableKeepsTheMount`, acrescente:

```cpp
// a helper that ends with its mount on one root, which stays in the mount table until
// the test empties `table` (the --list-mounts of the fake prints it)
static std::string endingHelperScript(const fs::path& table, const fs::path& listed)
{
  return R"([ "$1" = "--cleanup" ] && exit 0
if [ "$1" = "--list-mounts" ]; then echo x >> ')" +
         listed.string() + R"('; cat ')" + table.string() + R"('; exit 0; fi
echo '{"event":"hello","version":"0"}'
read line
echo '{"event":"ready","roots":["/x/Data"]}'
sleep 0.2
exit 1
)";
}

// the helper ended and its mount could not be unmounted: the mount table is checked
// again on a timer, and once the root is gone the file system is inactive and the
// deferred refresh is called back, once; the recheck stops then
TEST_F(VfsConnectorTest, RecheckClearsALeftMountOnceItIsGone)
{
  const auto table  = m_state.path() / "mounts.json";
  const auto listed = m_state.path() / "listed";
  writeFile(table, R"({"roots":["/x/Data"]})");
  fakeHelper(endingHelperScript(table, listed));
  qputenv("MO2_VFS_RECHECK_MS", "100");

  const auto vfs = createVfsConnector();
  int called     = 0;
  vfs->setDeactivatedCallback([&] {
    ++called;
  });

  vfs->activate();
  processEvents(600);  // the helper ends; its root is still listed
  ASSERT_TRUE(vfs->isActive());
  ASSERT_TRUE(vfs->staleMounts().contains("/x/Data"));

  processEvents(400);  // rechecked: still mounted
  EXPECT_TRUE(vfs->isActive());
  EXPECT_EQ(called, 0);

  writeFile(table, R"({"roots":[]})");
  EXPECT_TRUE(vfs->waitInactive(std::chrono::seconds(3)));
  processEvents(100);
  EXPECT_EQ(called, 1);

  // no more rechecks
  const auto runs = readFile(listed).size();
  processEvents(400);
  EXPECT_EQ(readFile(listed).size(), runs);
}

// closing Mod Organizer while a recheck runs: the process is killed, and nothing is
// called on the destroyed connector
TEST_F(VfsConnectorTest, DestroyedWhileRecheckingDoesNotCrash)
{
  const auto table  = m_state.path() / "mounts.json";
  const auto listed = m_state.path() / "listed";
  writeFile(table, R"({"roots":["/x/Data"]})");
  fakeHelper(R"([ "$1" = "--cleanup" ] && exit 0
if [ "$1" = "--list-mounts" ]; then echo x >> ')" +
             listed.string() + R"('; sleep 5; exit 0; fi
echo '{"event":"hello","version":"0"}'
read line
echo '{"event":"ready","roots":["/x/Data"]}'
sleep 0.2
exit 1
)");
  qputenv("MO2_VFS_RECHECK_MS", "100");

  int called = 0;
  {
    auto vfs = createVfsConnector();
    vfs->setDeactivatedCallback([&] {
      ++called;
    });
    vfs->activate();
    // the helper ends; cleanupLeftMounts() runs the slow --list-mounts once, then a
    // recheck starts it again
    processEvents(600);
    for (int i = 0; i < 100 && readFile(listed).size() < 4; ++i) {
      processEvents(100);
    }
    ASSERT_GE(readFile(listed).size(), 4u);  // two runs: "x\n" each
  }
  processEvents(200);
  EXPECT_EQ(called, 0);
}
```

Observação sobre o segundo teste: o `--list-mounts` lento faz o `cleanupLeftMounts()` síncrono esperar até o prazo de 10 s dele (`mountedRoots()`): o laço espera até 10 s pela segunda execução, a da reconferência. Se `mountedRoots()` desistir antes por timeout e contar a tabela como ilegível, as raízes continuam deixadas montadas e a reconferência roda do mesmo jeito.

- [ ] **Step 2: Rodar e ver falhar**

Run (depois de compilar): `distrobox enter mo2-run -- bash -c 'cd build-out/modorganizer && cmake --build . --target organizer_tests && TMPDIR=/tmp/mo2t ctest --output-on-failure -R "Recheck|Destroyed|UnreadableMountTable"'`
Expected: `RecheckClearsALeftMountOnceItIsGone` falha em `waitInactive` (nada reconfere); `DestroyedWhileRecheckingDoesNotCrash` falha no `ASSERT_GE` (só uma execução).

- [ ] **Step 3: Implementar**

Em `vfsconnector_linux.cpp`:

1. Includes: acrescente `#include <functional>` depois de `#include <QTimer>`.

2. No namespace anônimo, antes de `std::optional<QStringList> mountedRoots()`, acrescente:

```cpp
// the roots listed by `mo2-vfs --list-mounts` (its line {"roots":[...]}); nothing when
// there is no such line
std::optional<QStringList> parseMountedRoots(const QByteArray& output)
{
  for (const auto& line : output.split('\n')) {
    const auto doc = QJsonDocument::fromJson(line.trimmed());
    if (!doc.isObject() || !doc.object().contains("roots")) {
      continue;
    }

    QStringList roots;
    for (const auto& r : doc.object().value("roots").toArray()) {
      roots.push_back(r.toString());
    }

    return roots;
  }

  return std::nullopt;
}
```

e, em `mountedRoots()`, troque o laço final (do `for (const auto& line : process.readAllStandardOutput().split('\n')) {` até o `return roots;\n  }` que o fecha) por:

```cpp
  if (auto roots = parseMountedRoots(process.readAllStandardOutput())) {
    return roots;
  }
```

(o `log::warn(... did not list the mounts ...)` e o `return std::nullopt;` seguintes ficam).

3. Construtor e destrutor de `LinuxVfsConnector`: troque

```cpp
  explicit LinuxVfsConnector(QStringList stale) : m_stale(std::move(stale)) {}
```

por

```cpp
  explicit LinuxVfsConnector(QStringList stale) : m_stale(std::move(stale))
  {
    QObject::connect(&m_recheck, &QTimer::timeout, &m_context, [this] {
      recheckLeftMounts();
    });
  }

  // a recheck that is running is killed, without its callbacks
  ~LinuxVfsConnector() override
  {
    m_recheck.stop();

    if (m_recheckProcess != nullptr) {
      m_recheckProcess->disconnect();
      m_recheckProcess->kill();
      m_recheckProcess->waitForFinished(2000);
      delete m_recheckProcess;
      m_recheckProcess = nullptr;
    }
  }
```

4. Em `doActivate()`, no bloco de sucesso do mount, depois de `m_leftMounted.clear();` (o que segue `m_pendingDeactivation = false;`), acrescente `m_recheck.stop();`.

5. Em `keepLeftMounted()`, no fim (depois do laço que preenche `m_stale`), acrescente:

```cpp
    // checked again until the roots are gone (recheckLeftMounts())
    if (!m_recheck.isActive()) {
      m_recheck.start(vfsRecheckInterval());
    }
```

6. Em `deactivated()`, como primeira linha (antes do `if (!m_active && m_leftMounted.isEmpty())`), acrescente `m_recheck.stop();`.

7. Depois de `keepLeftMounted()`, acrescente os dois métodos:

```cpp
  // while roots are left mounted (keepLeftMounted()), the mount table is checked again
  // every vfsRecheckInterval(), without blocking the event loop: `mo2-vfs --cleanup`
  // (which only unmounts the roots of helpers that are gone), then `--list-mounts`;
  // once none of the roots is listed, the file system is inactive (deactivated(), which
  // runs the deferred refresh); nothing changes when the table can't be read
  void recheckLeftMounts()
  {
    if (m_recheckProcess != nullptr || m_leftMounted.isEmpty()) {
      return;
    }

    runHelperAsync({"--cleanup"}, [this](bool, const QByteArray&) {
      runHelperAsync({"--list-mounts"}, [this](bool ok, const QByteArray& output) {
        const auto mounted = ok ? parseMountedRoots(output) : std::nullopt;
        if (!mounted || m_leftMounted.isEmpty()) {
          return;
        }

        for (const auto& root : m_leftMounted) {
          if (mounted->contains(root)) {
            return;
          }
        }

        log::info("the virtual file system is no longer mounted on {}",
                  m_leftMounted.join(", "));
        m_cleanupNeeded = false;
        deactivated();
      });
    });
  }

  // runs the helper with `args` (vfsbridge::helperCommand()) without waiting for it;
  // `done` gets whether it exited with 0 and its standard output; a run that takes more
  // than 10 s is killed (done(false, ...)); `done` is not called when it can't be
  // started
  void runHelperAsync(const QStringList& args,
                      std::function<void(bool, const QByteArray&)> done)
  {
    vfsbridge::Command cmd;
    try {
      cmd = vfsbridge::helperCommand(vfsBinary(), args);
    } catch (const std::exception& e) {
      log::warn("cannot run mo2-vfs {}: {}", args.join(' '), e.what());
      return;
    }

    auto* process    = new QProcess(&m_context);
    m_recheckProcess = process;
    process->setProcessChannelMode(QProcess::SeparateChannels);

    auto* deadline = new QTimer(process);
    deadline->setSingleShot(true);
    QObject::connect(deadline, &QTimer::timeout, process, [process, args] {
      log::warn("mo2-vfs {} did not finish in time", args.join(' '));
      process->kill();
    });

    QObject::connect(process, &QProcess::errorOccurred, &m_context,
                     [this, process, args](QProcess::ProcessError error) {
                       if (error != QProcess::FailedToStart) {
                         return;
                       }
                       log::warn("cannot run mo2-vfs {}: {}", args.join(' '),
                                 process->errorString());
                       m_recheckProcess = nullptr;
                       process->deleteLater();
                     });

    QObject::connect(process, &QProcess::finished, &m_context,
                     [this, process, done = std::move(done)](
                         int code, QProcess::ExitStatus status) {
                       m_recheckProcess = nullptr;
                       process->deleteLater();
                       done(status == QProcess::NormalExit && code == 0,
                            process->readAllStandardOutput());
                     });

    process->start(cmd.program, cmd.arguments);
    process->closeWriteChannel();
    deadline->start(10000);
  }
```

8. Membros: depois de `QObject m_context;`, acrescente:

```cpp

  // rechecks the roots left mounted (recheckLeftMounts()), and its running process
  QTimer m_recheck;
  QProcess* m_recheckProcess = nullptr;
```

- [ ] **Step 4: Rodar e ver passar**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only modorganizer --tests`
Expected: toda a `organizer_tests` passa (incluindo os dois testes novos e `SpawnAfterLostHelperKeepsTheLeftMountGuard`, `UnreadableMountTableKeepsTheMount`, `FailedAutoUnmountKeepsActive`).

- [ ] **Step 5: Commit**

```bash
git add deps/modorganizer/src/linux/vfsconnector_linux.cpp deps/modorganizer/tests/test_vfsconnector.cpp
git commit -m "deps/modorganizer: Linux: recheck a mount left behind until it is gone

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL"
```

---

### Task 3: Helper travado com programa observado (P7)

**Files:**
- Modify: `deps/modorganizer/src/linux/vfsconnector_linux.cpp` (`helperLost()`, `doActivate()`, `recheckLeftMounts()`; novo `dropLostHelper()`)
- Test: `deps/modorganizer/tests/test_vfsconnector.cpp`

**Interfaces:**
- Consumes: `recheckLeftMounts()`, `keepLeftMounted()`, `m_recheck` (Task 2).
- Produces: `void dropLostHelper();` (privado): mata (`SIGKILL`) e descarta um `m_client` que não está mais utilizável (`!running()`) mas ainda roda; descarta um que já saiu; não faz nada com um utilizável.

- [ ] **Step 1: Testes que falham**

Em `test_vfsconnector.cpp`, depois de `DestroyedWhileRecheckingDoesNotCrash`, acrescente:

```cpp
// the first helper mounts /x/Data, starts a program (pid 1111) and then stops answering
// (exec sleep: alive, never reads again); the next ones answer every request. The
// mount table is the file mounts.json of `dir`, the number of helpers started is in
// `dir`/count and the pid of the first one in `dir`/fake.pid
static std::string hungHelperScript(const fs::path& dir)
{
  const auto table = (dir / "mounts.json").string();
  const auto count = (dir / "count").string();
  const auto pid   = (dir / "fake.pid").string();
  return R"([ "$1" = "--cleanup" ] && exit 0
if [ "$1" = "--list-mounts" ]; then cat ')" +
         table + R"('; exit 0; fi
n=$(( $(cat ')" +
         count + R"(' 2>/dev/null || echo 0) + 1 ))
echo $n > ')" +
         count + R"('
echo '{"event":"hello","version":"0"}'
if [ $n = 1 ]; then
  echo $$ > ')" +
         pid + R"('
  read line
  echo '{"event":"ready","roots":["/x/Data"]}'
  read line
  echo '{"event":"spawned","pid":1111}'
  exec sleep 30
fi
while read line; do
  echo '{"event":"ready","roots":["/x/Data"]}'
done
)";
}

// mo2-vfs stops answering while a program it started uses the mount: it is not killed,
// and its root counts as still mounted, so that the deferred refresh doesn't walk a
// hung mount; once the mount table no longer lists it, the hung helper is killed, the
// file system is inactive and the refresh is called back; the next activation gets a
// new helper
TEST_F(VfsConnectorTest, HungHelperWithAProgramKeepsTheMountUntilItIsGone)
{
  writeFile(m_state.path() / "mounts.json", R"({"roots":["/x/Data"]})");
  fakeHelper(hungHelperScript(m_state.path()));
  qputenv("MO2_VFS_TIMEOUT_MS", "300");
  qputenv("MO2_VFS_RECHECK_MS", "100");

  const auto vfs = createVfsConnector();
  int called     = 0;
  vfs->setDeactivatedCallback([&] {
    ++called;
  });

  VfsSpawnRequest r;
  r.argv             = {"true"};
  r.workingDirectory = "/";

  vfs->activate();
  EXPECT_EQ(vfs->spawn(r), 1111);

  vfs->updateMapping({});  // the update times out (and is only logged)

  const auto fakePid = std::stoi(readFile(m_state.path() / "fake.pid"));
  processEvents(400);
  EXPECT_EQ(::kill(fakePid, 0), 0);  // not killed
  EXPECT_TRUE(vfs->isActive());
  EXPECT_EQ(called, 0);
  EXPECT_TRUE(vfs->staleMounts().contains("/x/Data"));
  const auto reason = vfs->takeLostReason();
  EXPECT_TRUE(reason.contains("did not reply")) << reason.toStdString();

  // the mount is gone
  writeFile(m_state.path() / "mounts.json", R"({"roots":[]})");
  EXPECT_TRUE(vfs->waitInactive(std::chrono::seconds(3)));
  processEvents(100);
  EXPECT_EQ(called, 1);
  EXPECT_NE(::kill(fakePid, 0), 0);  // killed and reaped

  vfs->activate();
  EXPECT_TRUE(vfs->isActive());
  EXPECT_EQ(readFile(m_state.path() / "count"), "2\n");
}

// a hooked Run while the hung helper's root is still mounted fails and keeps the hung
// helper (a program may be using its mount); once the root is gone (before any
// recheck), the next activation kills it and mounts with a new helper
TEST_F(VfsConnectorTest, ActivateKillsTheHungHelperOnlyOnceItsMountIsGone)
{
  writeFile(m_state.path() / "mounts.json", R"({"roots":["/x/Data"]})");
  fakeHelper(hungHelperScript(m_state.path()));
  qputenv("MO2_VFS_TIMEOUT_MS", "300");  // no recheck runs: its default is 5 s

  const auto vfs = createVfsConnector();

  VfsSpawnRequest r;
  r.argv             = {"true"};
  r.workingDirectory = "/";

  vfs->activate();
  EXPECT_EQ(vfs->spawn(r), 1111);
  vfs->updateMapping({});  // times out

  const auto fakePid = std::stoi(readFile(m_state.path() / "fake.pid"));

  try {
    vfs->activate();
    FAIL() << "activate() mounted over the mount of the hung helper";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("still mounted"), std::string::npos)
        << e.what();
  }
  EXPECT_TRUE(vfs->isActive());
  EXPECT_EQ(::kill(fakePid, 0), 0);  // kept

  writeFile(m_state.path() / "mounts.json", R"({"roots":[]})");
  vfs->activate();
  EXPECT_TRUE(vfs->isActive());
  EXPECT_NE(::kill(fakePid, 0), 0);  // killed and reaped
  EXPECT_EQ(readFile(m_state.path() / "count"), "2\n");
}
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `distrobox enter mo2-run -- bash -c 'cd build-out/modorganizer && cmake --build . --target organizer_tests && TMPDIR=/tmp/mo2t ctest --output-on-failure -R "HungHelper|ActivateKillsTheHungHelper"'`
Expected: `HungHelperWithAProgramKeepsTheMountUntilItIsGone` falha em `EXPECT_TRUE(vfs->isActive())` (hoje `deactivated()` roda) e `called` vale 1; `ActivateKillsTheHungHelperOnlyOnceItsMountIsGone` falha (o primeiro `activate()` não lança, ou o pid continua vivo no fim).

- [ ] **Step 3: Implementar**

Em `vfsconnector_linux.cpp`:

1. Em `helperLost()`, troque o bloco

```cpp
    if (m_client && !m_client->exited() && m_watched > 0) {
      // not killed: a program may still be using the mount; whatever the helper left
      // behind is cleaned up before the next one starts
      log::error("mo2-vfs stopped answering while programs use the virtual file "
                 "system: {}",
                 reason);
      m_cleanupNeeded = true;
      deactivated();
      return;
    }
```

por

```cpp
    if (m_client && !m_client->exited() && m_watched > 0) {
      // not killed: a program may still be using the mount, whose roots count as still
      // mounted (the deferred refresh must not walk a hung mount) until the mount
      // table no longer lists them (recheckLeftMounts()) or the helper recovers and
      // unmounts; the helper is killed once its mount is gone (dropLostHelper())
      log::error("mo2-vfs stopped answering while programs use the virtual file "
                 "system: {}",
                 reason);
      m_cleanupNeeded = true;

      if (m_roots.isEmpty()) {
        deactivated();
      } else {
        keepLeftMounted(m_roots, "it is still mounted on " + m_roots.join(", ") +
                                     "; the data directory is refreshed once it is "
                                     "unmounted");
      }
      return;
    }
```

2. Em `doActivate()`, troque o começo

```cpp
    if (m_client && !m_client->running()) {
      helperLost("mo2-vfs is not running");
      m_client.reset();
    }
```

por

```cpp
    if (m_client && !m_client->running()) {
      helperLost("mo2-vfs is not running");

      // one that stopped answering but still runs may hold the mount of a running
      // program: kept until its mount is gone (dropLostHelper() below)
      if (m_client->exited()) {
        m_client.reset();
      }
    }
```

e, logo depois do bloco `if (!m_active && (m_cleanupNeeded || !m_leftMounted.isEmpty())) { ... }` (o que lança "still mounted"), antes de `// a new helper has nothing mounted ...`, acrescente:

```cpp
    // nothing of a lost helper is mounted any more
    dropLostHelper();
```

3. Em `recheckLeftMounts()` (Task 2), depois de `deactivated();`, acrescente `dropLostHelper();`.

4. Depois de `runHelperAsync()`, acrescente:

```cpp
  // a helper that was lost (it ended, or stopped answering and still runs) is
  // discarded; one that still runs is killed first: only called once its mount is gone,
  // so that no program can be using it, and the signal goes to the helper only (its
  // programs are in process groups of their own)
  void dropLostHelper()
  {
    if (!m_client || m_client->running()) {
      return;
    }

    if (!m_client->exited()) {
      log::debug("killing mo2-vfs, which stopped answering, now that its mount is gone");
      m_client->kill();
    }

    m_client.reset();
  }
```

- [ ] **Step 4: Rodar e ver passar**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only modorganizer --tests`
Expected: toda a `organizer_tests` passa, incluindo `UpdateTimeoutDeactivatesAndRemounts` (sem programa observado: o helper travado continua sendo morto na hora), `HelperCrashWithWatchedGroupCleansUp` e `SpawnAfterLostHelperKeepsTheLeftMountGuard`.

- [ ] **Step 5: Commit**

```bash
git add deps/modorganizer/src/linux/vfsconnector_linux.cpp deps/modorganizer/tests/test_vfsconnector.cpp
git commit -m "deps/modorganizer: Linux: a hung helper with programs keeps its mount until it is gone

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL"
```

---

### Task 4: Sem tela, mensagem clara (P1)

**Files:**
- Create: `deps/modorganizer/src/linux/displaycheck_linux.h`
- Create: `deps/modorganizer/src/linux/displaycheck_linux.cpp`
- Modify: `deps/modorganizer/src/CMakeLists.txt` (`_organizer_desktop_sources`)
- Modify: `deps/modorganizer/src/main.cpp`
- Test: `deps/modorganizer/tests/test_displaycheck.cpp` (novo; o `mo2_configure_tests` pega todo `.cpp` da pasta)
- Modify: `tests/smoke/modorganizer-smoke.sh`

**Interfaces:**
- Produces: `QString missingDisplayMessage(const QStringList& args, const QProcessEnvironment& env);` — `args` é a linha de comando com o programa primeiro; devolve a mensagem exata dos Global Constraints ou uma string vazia.

- [ ] **Step 1: Teste que falha**

`deps/modorganizer/tests/test_displaycheck.cpp`:

```cpp
// the display check of main() on Linux: QApplication aborts without a display, so the
// missing display is reported before it is created

#include <gtest/gtest.h>

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <initializer_list>
#include <utility>

#include "linux/displaycheck_linux.h"

namespace
{
QProcessEnvironment environment(std::initializer_list<std::pair<QString, QString>> vars)
{
  QProcessEnvironment env;
  for (const auto& [name, value] : vars) {
    env.insert(name, value);
  }
  return env;
}

const QStringList NoArgs{"ModOrganizer"};
}  // namespace

TEST(DisplayCheck, NoDisplayIsReported)
{
  const auto message = missingDisplayMessage(NoArgs, environment({}));
  EXPECT_TRUE(message.startsWith("Mod Organizer needs a graphical session"))
      << message.toStdString();
  EXPECT_TRUE(message.contains("XAUTHORITY")) << message.toStdString();
}

TEST(DisplayCheck, WaylandOrX11DisplayIsEnough)
{
  EXPECT_TRUE(missingDisplayMessage(NoArgs, environment({{"WAYLAND_DISPLAY", "wayland-0"}}))
                  .isEmpty());
  EXPECT_TRUE(missingDisplayMessage(NoArgs, environment({{"DISPLAY", ":0"}})).isEmpty());
}

TEST(DisplayCheck, EmptyVariablesAreNoDisplay)
{
  EXPECT_FALSE(
      missingDisplayMessage(NoArgs, environment({{"WAYLAND_DISPLAY", ""}, {"DISPLAY", ""}}))
          .isEmpty());
}

TEST(DisplayCheck, PlatformsWithoutADisplayPass)
{
  for (const char* platform : {"offscreen", "minimal", "vnc"}) {
    EXPECT_TRUE(
        missingDisplayMessage(NoArgs, environment({{"QT_QPA_PLATFORM", platform}}))
            .isEmpty())
        << platform;
  }
}

TEST(DisplayCheck, WaylandAndXcbPlatformsNeedADisplay)
{
  for (const char* platform : {"wayland", "xcb", "wayland;xcb", "wayland-egl"}) {
    EXPECT_FALSE(
        missingDisplayMessage(NoArgs, environment({{"QT_QPA_PLATFORM", platform}}))
            .isEmpty())
        << platform;
  }
}

TEST(DisplayCheck, PlatformArgumentWinsOverTheVariable)
{
  EXPECT_TRUE(missingDisplayMessage({"ModOrganizer", "-platform", "offscreen"},
                                    environment({{"QT_QPA_PLATFORM", "xcb"}}))
                  .isEmpty());
  EXPECT_TRUE(missingDisplayMessage({"ModOrganizer", "--platform", "offscreen"},
                                    environment({}))
                  .isEmpty());
  EXPECT_FALSE(missingDisplayMessage({"ModOrganizer", "-platform", "xcb"},
                                     environment({{"QT_QPA_PLATFORM", "offscreen"}}))
                   .isEmpty());
}

TEST(DisplayCheck, PlatformArgumentWithoutValueIsIgnored)
{
  EXPECT_TRUE(missingDisplayMessage({"ModOrganizer", "-platform"},
                                    environment({{"QT_QPA_PLATFORM", "offscreen"}}))
                  .isEmpty());
}
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only modorganizer --tests`
Expected: erro de compilação (`linux/displaycheck_linux.h` não existe).

- [ ] **Step 3: Implementar**

`deps/modorganizer/src/linux/displaycheck_linux.h`:

```cpp
#ifndef DISPLAYCHECK_LINUX_H
#define DISPLAYCHECK_LINUX_H

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

// why Mod Organizer can't show its windows, checked before its QApplication exists
// (which aborts without a display): the Qt platform is the one of `-platform <p>` or
// `--platform <p>` in `args` (the command line, the program first), else
// QT_QPA_PLATFORM; with none, or one that starts with "wayland" or "xcb",
// WAYLAND_DISPLAY or DISPLAY must be set. Returns the message to show, or an empty
// string.
QString missingDisplayMessage(const QStringList& args, const QProcessEnvironment& env);

#endif  // DISPLAYCHECK_LINUX_H
```

`deps/modorganizer/src/linux/displaycheck_linux.cpp`:

```cpp
#include "displaycheck_linux.h"

namespace
{
// the Qt platform asked for: the last -platform/--platform argument with a value, else
// QT_QPA_PLATFORM
QString platform(const QStringList& args, const QProcessEnvironment& env)
{
  QString fromArgs;
  for (qsizetype i = 1; i + 1 < args.size(); ++i) {
    if (args[i] == "-platform" || args[i] == "--platform") {
      fromArgs = args[i + 1];
    }
  }

  return (fromArgs.isEmpty() ? env.value("QT_QPA_PLATFORM") : fromArgs).trimmed();
}
}  // namespace

QString missingDisplayMessage(const QStringList& args, const QProcessEnvironment& env)
{
  const auto p = platform(args, env);

  const bool needsDisplay =
      p.isEmpty() || p.startsWith("wayland") || p.startsWith("xcb");

  if (!needsDisplay || !env.value("WAYLAND_DISPLAY").isEmpty() ||
      !env.value("DISPLAY").isEmpty()) {
    return {};
  }

  return "Mod Organizer needs a graphical session, but neither WAYLAND_DISPLAY nor "
         "DISPLAY is set. Run it from the desktop, or set WAYLAND_DISPLAY, "
         "XDG_RUNTIME_DIR, DISPLAY and XAUTHORITY to those of the desktop session.";
}
```

Em `deps/modorganizer/src/CMakeLists.txt`, troque

```cmake
set(_organizer_desktop_sources
	linux/nxmhandler_linux.cpp
	linux/secretstore_linux.cpp)
```

por

```cmake
set(_organizer_desktop_sources
	linux/displaycheck_linux.cpp
	linux/nxmhandler_linux.cpp
	linux/secretstore_linux.cpp)
```

e, no comentário logo acima (`# the desktop integration of the Linux build: logins ... spec of Phase 4a`), acrescente uma linha: `# and the display check of main() (displaycheck_linux.cpp, Phase 4c.2)`.

Em `deps/modorganizer/src/main.cpp`:

1. Depois de `#include <report.h>`, acrescente:

```cpp

#ifndef _WIN32
#include "linux/displaycheck_linux.h"
#include <QProcessEnvironment>
#endif
```

2. Em `run()`, depois de `initLogging();` e antes de `// must be after logging`, acrescente:

```cpp
#ifndef _WIN32
  // without a display, QApplication aborts, which looks like a crash
  {
    QStringList args;
    for (int i = 0; i < argc; ++i) {
      args.push_back(QString::fromLocal8Bit(argv[i]));
    }

    const auto why =
        missingDisplayMessage(args, QProcessEnvironment::systemEnvironment());
    if (!why.isEmpty()) {
      log::error("{}", why);
      return 1;
    }
  }
#endif

```

Em `tests/smoke/modorganizer-smoke.sh`:

1. No cabeçalho, depois do item `# 1. ...` (as duas linhas sobre `--help`), acrescente:

```bash
# 1b. without a display (WAYLAND_DISPLAY, DISPLAY and QT_QPA_PLATFORM unset), it exits
#    with 1 and says it needs a graphical session, without crashing (Phase 4c.2);
```

2. Depois do bloco do `--help` (a linha `[[ $out == *instance* ]] || fail "--help did not print the usage"`), acrescente:

```bash

echo "--- ModOrganizer without a display"
rc=0
nd_home="$(mktemp -d)"
mkdir -m 700 "$nd_home/runtime"
out="$(env -u WAYLAND_DISPLAY -u DISPLAY -u QT_QPA_PLATFORM HOME="$nd_home" \
    XDG_CONFIG_HOME="$nd_home/.config" XDG_DATA_HOME="$nd_home/.local/share" \
    XDG_CACHE_HOME="$nd_home/.cache" XDG_STATE_HOME="$nd_home/.local/state" \
    XDG_RUNTIME_DIR="$nd_home/runtime" MO2_VFS_STATE_DIR="$nd_home/runtime/mo2-vfs" \
    DBUS_SESSION_BUS_ADDRESS="unix:path=$nd_home/no-bus" \
    timeout -k 5 20 "$APP" 2>&1)" || rc=$?
rm -rf "$nd_home"
printf '%s\n' "$out"
((rc == 1)) || fail "without a display, it exited with $rc instead of 1"
[[ $out == *"needs a graphical session"* ]] || fail "without a display, no message"
[[ $out != *"has crashed"* && $out != *"terminate called"* ]] ||
    fail "without a display, the application crashed"
```

- [ ] **Step 4: Rodar e ver passar**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only modorganizer --tests`
Expected: `organizer_tests` passa (os 7 `DisplayCheck.*`), e o smoke (que o `build.sh` roda depois, com `--only modorganizer`, se o `game_bethesda` estiver instalado no `install/` do worktree; senão rode `tests/smoke/modorganizer-smoke.sh install` dentro do box, com o TMPDIR padrão) mostra `--- ModOrganizer without a display` seguido da mensagem e continua até o fim.

Conferência manual rápida, dentro do box: `env -u WAYLAND_DISPLAY -u DISPLAY -u QT_QPA_PLATFORM install/bin/ModOrganizer; echo $?` → a mensagem e `1`.

- [ ] **Step 5: Commits**

```bash
git add deps/modorganizer/src/linux/displaycheck_linux.h deps/modorganizer/src/linux/displaycheck_linux.cpp \
  deps/modorganizer/src/CMakeLists.txt deps/modorganizer/src/main.cpp deps/modorganizer/tests/test_displaycheck.cpp
git commit -m "deps/modorganizer: Linux: say that there is no display instead of crashing

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL"
git add tests/smoke/modorganizer-smoke.sh
git commit -m "tests: smoke: without a display, ModOrganizer exits with a message

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL"
```

---

### Task 5: INIs de arquivos e invalidação de BSA ignorando maiúsculas (I1, I2)

**Files:**
- Modify: `deps/game_bethesda/src/gamebryo/gamebryodataarchives.cpp` (`getArchivesFromKey`, `setArchivesToKey`)
- Modify: `deps/game_bethesda/src/games/morrowind/morrowinddataarchives.cpp` (`getArchives`, `setArchives`)
- Modify: `deps/game_bethesda/src/gamebryo/gamebryobsainvalidation.cpp` (linha 51)
- Modify: `deps/game_bethesda/src/games/fallout4/fallout4bsainvalidation.cpp` (linha 39)
- Modify: `deps/game_bethesda/src/games/fallout4london/fo4londonbsainvalidation.cpp` (linha 39)
- Modify: `deps/game_bethesda/src/games/starfield/starfieldbsainvalidation.cpp` (linha 39)
- Test: `deps/game_bethesda/tests/test_profile_inis_linux.cpp`

**Interfaces:**
- Consumes: `static QString GameGamebryo::resolveIgnoringCase(const QString& filePath);` (4c.1, `gamebryo/gamegamebryo.h`): o caminho de um arquivo existente com o mesmo nome ignorando maiúsculas na mesma pasta (preferindo o nome exato), senão o caminho dado; identidade no Windows.

- [ ] **Step 1: Testes que falham**

Em `deps/game_bethesda/tests/test_profile_inis_linux.cpp`:

1. Atualize o comentário do topo: troque `// the repair of a profile when it is opened.` por `// the repair of a profile when it is opened, the archive lists and the BSA invalidation.`
2. Depois de `#include "gamebryolocalsavegames.h"`, acrescente:

```cpp
#include "gamebryobsainvalidation.h"
#include "gamebryodataarchives.h"
```

3. No namespace anônimo, depois da classe `LocalSaves` (antes do `}  // namespace`), acrescente:

```cpp
// the archive lists of a game with one INI, like Skyrim (skyrimdataarchives.cpp):
// fake.ini, in the profile or in the documents of the game
class FakeArchives : public GamebryoDataArchives
{
public:
  using GamebryoDataArchives::GamebryoDataArchives;

  QStringList vanillaArchives() const override { return {}; }

  QStringList archives(const IProfile* profile) const override
  {
    return getArchivesFromKey(iniFile(profile), "SResourceArchiveList");
  }

private:
  QString iniFile(const IProfile* profile) const
  {
    return profile->localSettingsEnabled()
               ? QDir(profile->absolutePath()).absoluteFilePath("fake.ini")
               : localGameDirectory().absoluteFilePath("fake.ini");
  }

  void writeArchiveList(IProfile* profile, const QStringList& before) override
  {
    setArchivesToKey(iniFile(profile), "SResourceArchiveList", before.join(", "));
  }
};

class FakeInvalidation : public GamebryoBSAInvalidation
{
public:
  using GamebryoBSAInvalidation::GamebryoBSAInvalidation;

private:
  QString invalidationBSAName() const override { return "Fake - Invalidation.bsa"; }
  unsigned long bsaVersion() const override { return 0x68; }
};

// the documents of the game and a profile, as for the local saves
class IniCase : public LocalSaves
{};
```

4. No fim do arquivo, acrescente:

```cpp
// the Proton prefix has Fake.ini, the game reads fake.ini: the archive list is read
// from and written to the file that exists
TEST_F(IniCase, ArchivesUseThePrefixIniWithAnotherCase)
{
  ASSERT_TRUE(write(m_myGames + "/Fake.ini",
                    "[Archive]\r\nSResourceArchiveList=A.bsa, B.bsa\r\n"));
  FakeGame game(m_myGames);
  FakeProfile profile(m_profile, false, false);
  FakeArchives archives(&game);

  EXPECT_EQ(archives.archives(&profile), (QStringList{"A.bsa", "B.bsa"}));

  archives.addArchive(&profile, 0, "C.bsa");

  const QByteArray ini = read(m_myGames + "/Fake.ini");
  EXPECT_TRUE(ini.contains("SResourceArchiveList=C.bsa, A.bsa, B.bsa"))
      << ini.toStdString();
  EXPECT_FALSE(QFile::exists(m_myGames + "/fake.ini"));
}

TEST_F(IniCase, ArchivesUseTheProfileIniWithAnotherCase)
{
  ASSERT_TRUE(
      write(m_profile + "/FAKE.ini", "[Archive]\r\nSResourceArchiveList=A.bsa\r\n"));
  FakeGame game(m_myGames);
  FakeProfile profile(m_profile, true, false);
  FakeArchives archives(&game);

  archives.removeArchive(&profile, "A.bsa");
  archives.addArchive(&profile, 0, "B.bsa");

  const QByteArray ini = read(m_profile + "/FAKE.ini");
  EXPECT_TRUE(ini.contains("SResourceArchiveList=B.bsa")) << ini.toStdString();
  EXPECT_FALSE(ini.contains("A.bsa")) << ini.toStdString();
  EXPECT_FALSE(QFile::exists(m_profile + "/fake.ini"));
}

// no INI in any case (a new profile): created with the name asked for, as before
TEST_F(IniCase, ArchivesCreateTheIniWithTheNameAskedFor)
{
  FakeGame game(m_myGames);
  FakeProfile profile(m_profile, true, false);
  FakeArchives archives(&game);

  archives.addArchive(&profile, 0, "C.bsa");

  const QByteArray ini = read(m_profile + "/fake.ini");
  EXPECT_TRUE(ini.contains("SResourceArchiveList=C.bsa")) << ini.toStdString();
}

// Fallout 4 names Fallout4Custom.ini, the profile has fallout4custom.ini (iniFiles()):
// the invalidation writes to the file of the profile, without a second one
TEST_F(IniCase, BsaInvalidationUsesTheProfileIniWithAnotherCase)
{
  ASSERT_TRUE(
      write(m_profile + "/fakecustom.ini", "[Display]\r\nbFull Screen=0\r\n"));
  FakeGame game(m_myGames);
  FakeProfile profile(m_profile, true, false);  // invalidation not active
  FakeArchives archives(&game);
  FakeInvalidation invalidation(&archives, "FakeCustom.ini", &game);

  EXPECT_TRUE(invalidation.prepareProfile(&profile));

  const QByteArray ini = read(m_profile + "/fakecustom.ini");
  EXPECT_TRUE(ini.contains("bFull Screen=0")) << ini.toStdString();
  EXPECT_TRUE(ini.contains("bInvalidateOlderFiles=1")) << ini.toStdString();
  EXPECT_TRUE(ini.contains("SInvalidationFile=ArchiveInvalidation.txt"))
      << ini.toStdString();
  EXPECT_FALSE(QFile::exists(m_profile + "/FakeCustom.ini"));
}
```

Se o escritor de INI do `uibase` no Linux grava as chaves com outro espaçamento (ex.: `key = value`), ajuste só as strings esperadas para o formato que ele grava (confira em `UseTheProfileIniWithAnotherCase`, que espera `sLocalSavePath=__MO_Saves\`): o formato é `chave=valor`.

- [ ] **Step 2: Rodar e ver falhar**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only game_bethesda --tests`
Expected: os quatro testes `IniCase.*` compilam; `ArchivesUseThePrefixIniWithAnotherCase`, `ArchivesUseTheProfileIniWithAnotherCase` e `BsaInvalidationUsesTheProfileIniWithAnotherCase` falham (a lista vem vazia e `fake.ini`/`FakeCustom.ini` são criados); `ArchivesCreateTheIniWithTheNameAskedFor` passa.

- [ ] **Step 3: Implementar**

1. `gamebryo/gamebryodataarchives.cpp`, em `getArchivesFromKey()`: troque

```cpp
  std::wstring iniFileW = QDir::toNativeSeparators(iniFile).toStdWString();
```

por

```cpp
  // the INI with another case (the Proton prefix has Skyrim.ini for skyrim.ini)
  std::wstring iniFileW =
      QDir::toNativeSeparators(GameGamebryo::resolveIgnoringCase(iniFile))
          .toStdWString();
```

e em `setArchivesToKey()` troque o corpo por:

```cpp
  const QString path = GameGamebryo::resolveIgnoringCase(iniFile);
  if (!MOBase::WriteRegistryValue(L"Archive", key.toStdWString().c_str(),
                                  value.toStdWString().c_str(),
                                  path.toStdWString().c_str())) {
    qWarning("failed to set archives in \"%s\"", qUtf8Printable(path));
  }
```

2. `games/morrowind/morrowinddataarchives.cpp`: em `getArchives()`, troque

```cpp
  std::wstring iniFileW = QDir::toNativeSeparators(iniFile).toStdWString();
```

por

```cpp
  std::wstring iniFileW =
      QDir::toNativeSeparators(GameGamebryo::resolveIgnoringCase(iniFile))
          .toStdWString();
```

e em `setArchives()` acrescente como primeira linha `const std::wstring iniFileW = GameGamebryo::resolveIgnoringCase(iniFile).toStdWString();` e troque as duas ocorrências de `iniFile.toStdWString().c_str()` da função por `iniFileW.c_str()` (o `qWarning` continua com `iniFile`).

3. Nos quatro `prepareProfile()` (`gamebryo/gamebryobsainvalidation.cpp`, `games/fallout4/fallout4bsainvalidation.cpp`, `games/fallout4london/fo4londonbsainvalidation.cpp`, `games/starfield/starfieldbsainvalidation.cpp`), troque

```cpp
  QString iniFilePath = basePath + "/" + m_IniFileName;
```

por

```cpp
  // the INI with another case (Fallout4Custom.ini is fallout4custom.ini in a profile)
  QString iniFilePath = GameGamebryo::resolveIgnoringCase(basePath + "/" + m_IniFileName);
```

e acrescente `#include "gamegamebryo.h"` depois de `#include "dummybsa.h"` em cada um deles (o `game_gamebryo` exporta a pasta `gamebryo/` como include público).

4. Conferência de cobertura (spec §4): `grep -rn "GetPrivateProfileString\|WriteRegistryValue\|WritePrivateProfile" deps/game_bethesda/src --include='*dataarchives.cpp' --include='*bsainvalidation.cpp'` deve listar só os seis arquivos acima, e em cada um o caminho do INI tem de passar por `resolveIgnoringCase()`. Um arquivo a mais nessa lista recebe a mesma troca.

- [ ] **Step 4: Rodar e ver passar**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only game_bethesda --tests`
Expected: `game_gamebryo_tests` e `game_bethesda_tests` passam (incluindo os quatro `IniCase.*` e os testes da 4c.1); os 16 plugins de jogo compilam.

- [ ] **Step 5: Commit**

```bash
git add deps/game_bethesda/src/gamebryo/gamebryodataarchives.cpp \
  deps/game_bethesda/src/games/morrowind/morrowinddataarchives.cpp \
  deps/game_bethesda/src/gamebryo/gamebryobsainvalidation.cpp \
  deps/game_bethesda/src/games/fallout4/fallout4bsainvalidation.cpp \
  deps/game_bethesda/src/games/fallout4london/fo4londonbsainvalidation.cpp \
  deps/game_bethesda/src/games/starfield/starfieldbsainvalidation.cpp \
  deps/game_bethesda/tests/test_profile_inis_linux.cpp
git commit -m "deps/game_bethesda: Linux: archive lists and BSA invalidation find INIs ignoring case

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL"
```

---

### Task 6: Documentação, checklist e ponto de controle de CI

**Files:**
- Modify: `docs/testing/phase2-manual-checklist.md`
- Modify: `README.md` (seção Bazzite, ~linha 97)
- Create: `docs/testing/phase4c2-manual-checklist.md`
- Modify: `STATE.md`
- Modify: `docs/superpowers/specs/2026-10-20-phase4c2-pendencias-design.md` (só o status, no fim da fase)

- [ ] **Step 1: Checklist da Fase 2 (P2–P5)**

Em `docs/testing/phase2-manual-checklist.md`:

1. P2, na seção 2: troque

```
  konsole: `--nofork -e /home/<usuário>/bin/vfs-shell.sh "<pasta do jogo>/Data"` (sem
  `--nofork` o Konsole pode entregar a janela a uma instância já aberta). Confirme com **"OK"**.
```

por

```
  konsole: `--separate --nofork -e /home/<usuário>/bin/vfs-shell.sh "<pasta do jogo>/Data"`
  (sem `--separate` o Konsole entrega a janela ao Konsole já aberto do host, pela sessão
  D-Bus, e sai na hora, e o VFS desmonta; sem `--nofork` ele se desliga do processo que o
  MO2 observa). Confirme com **"OK"**.
```

2. P3, na seção 0, depois do parágrafo que começa com `O `mo2-vfs` precisa ficar na mesma pasta` (e antes de `Para o teste você precisa de:`), acrescente:

```
**Rodando por SSH** (em vez do Konsole da área de trabalho): sem a sessão gráfica o MO2
não abre. Exporte as variáveis da sessão da área de trabalho antes do comando:
`WAYLAND_DISPLAY` (ex.: `wayland-0`), `XDG_SESSION_TYPE=wayland`,
`XDG_RUNTIME_DIR=/run/user/$UID`, `DISPLAY` (ex.: `:0`) e `XAUTHORITY` (o arquivo
`xauth_*` em `$XDG_RUNTIME_DIR`). Sem `XAUTHORITY`, programas X11 como o `xterm` falham
com "Authorization required". Mais simples: rodar do Konsole da área de trabalho.
```

3. P4, na seção 6, troque o item

```
- [ ] Repetir com o MO2 aberto pelo terminal: com o programa de teste aberto, apertar
  **Ctrl+C** no terminal do MO2 (ou fechar esse terminal).
  Esperado: o mount **continua** (`findmnt` dentro do box) e o programa de teste continua
  vendo os arquivos dos mods, feche o MO2 ou não: o `mo2-vfs` roda numa sessão própria e
  não recebe o Ctrl+C nem o fechamento do terminal.
```

por

```
- [ ] Repetir com o MO2 aberto pelo terminal: com o programa de teste aberto, apertar
  **Ctrl+C** no terminal do MO2 (ou fechar esse terminal).
  Esperado: o mount **continua** (`findmnt` dentro do box) e o programa de teste continua
  vendo os arquivos dos mods: o `mo2-vfs` roda numa sessão própria e não recebe o Ctrl+C
  nem o fechamento do terminal. Com o MO2 aberto por `distrobox enter ... | tee` (passo 3
  da preparação), o Ctrl+C (ou fechar o terminal) encerra só o cliente do podman: o MO2
  **continua aberto**, e o mount também. Feche o MO2 pela janela (**File -> "Exit"**).
```

4. P5, na seção 8, troque `nenhum `crash.log` novo.` por `nenhum `crash.log` novo **não vazio** (o MO2 cria um `crash.log` vazio ao iniciar; só um com conteúdo indica queda).`; em "O que enviar se falhar", item 3, troque `(`mo_interface.log` e, se existir, `crash.log`)` por `(`mo_interface.log` e, se não estiver vazio, `crash.log`)`.

- [ ] **Step 2: README**

Em `README.md`, depois do parágrafo que termina com ``distrobox enter mo2-run -- env QT_QPA_PLATFORM=xcb ~/mo2-phase1/bin/ModOrganizer`.`` (item 4 da seção Bazzite), acrescente, com o mesmo recuo do parágrafo anterior:

```
   Por SSH, exporte antes as variáveis da sessão da área de trabalho (`WAYLAND_DISPLAY`,
   `XDG_SESSION_TYPE`, `XDG_RUNTIME_DIR`, `DISPLAY`, `XAUTHORITY`); sem tela, o MO2 avisa
   ("needs a graphical session") e sai.
```

- [ ] **Step 3: Checklist manual da 4c.2**

Crie `docs/testing/phase4c2-manual-checklist.md` no estilo de `docs/testing/phase4c1-manual-checklist.md` (leia-o antes: cabeçalho, como obter o build — aqui o `install/` do worktree `ModOrganizerLinux-4c2` ou o artefato `mo2-linux-install` do run verde do PR —, "Preparação", itens numerados com caixas, "O que enviar se falhar"). Itens:

1. **Sem tela.** Por SSH (sem exportar nada), dentro do box: `~/<pasta do build>/bin/ModOrganizer; echo "saída $?"`. Esperado: a mensagem `Mod Organizer needs a graphical session, ...` e `saída 1`; nenhum "has crashed" nem backtrace.
2. **Arquivos (BSA/BA2) no INI certo.** Com o Skyrim SE: no perfil com **INIs por perfil desligados**, aba **Archives**, desmarcar e marcar de novo um BSA de mod com a opção de gerenciar arquivos ligada (ou ligar/desligar **Automatic Archive Invalidation** em **Profiles...**). Esperado: no prefixo, `.../My Games/Skyrim Special Edition/Skyrim.ini` (o arquivo real, com maiúscula) muda (`SResourceArchiveList`/`bInvalidateOlderFiles`), e **não** aparece um `skyrim.ini` minúsculo ao lado: `ls ".../My Games/Skyrim Special Edition/"`. Com o Fallout 4 instalado (opcional): o mesmo, conferindo `Fallout4Custom.ini`; com INIs por perfil ligados, `fallout4custom.ini` do perfil muda e não aparece `Fallout4Custom.ini` no perfil.
3. **Helper travado.** Executar o SKSE (ou o Shell VFS da Fase 2) pelo MO2. Com ele aberto, num terminal do host: `pkill -STOP -x mo2-vfs`. No MO2, provoque um refresh (F5, ou instalar/ativar um mod), que pede um `update` ao VFS: depois de ~30 s o MO2 registra no log `mo2-vfs stopped answering while programs use the virtual file system` e **não** mostra a lista de plugins vazia (o refresh fica adiado). Então `pkill -CONT -x mo2-vfs` e feche o programa. Esperado: o mount some (`findmnt -n -o TARGET,FSTYPE | grep mo2-vfs` vazio no host) e, em até ~5 s, o log mostra `the virtual file system is no longer mounted on ...` e o refresh adiado roda (lista de plugins completa). Fechar e abrir o MO2: sem aviso de mount órfão.
4. **Regressão rápida.** Skyrim SE + SKSE com mods pelo "Executar"; LOOT ordena; fechar o jogo desmonta.

"O que enviar se falhar": item, saída do terminal, `logs/` da instância (`mo_interface.log`, `mo2-vfs.log`, `crash.log` se não vazio), `findmnt -n -o TARGET,FSTYPE,SOURCE | grep mo2-vfs` no host, versão do build (commit).

- [ ] **Step 4: STATE.md**

1. Linha "Fase atual": `**Fase atual:** 4c.2 — Pendências da Fase 2 e INIs · **Status:** 🟡 implementação concluída; push, CI e teste manual pendentes · **Anterior:** 4c.1 — Plugins C++ restantes (🟡 PR #4; teste manual pendente)`.
2. Nova seção de progresso da 4c.2 no mesmo formato das tabelas da 4c.1 (Task | Item | Status | Commits), com as Tasks 1–7 deste plano e os hashes dos commits (`git log --oneline fase-4c1-plugins..HEAD`).
3. "Próximos passos": o item 4 passa a dizer que a 4c.2 está em `fase-4c2-pendencias` (worktree `../ModOrganizerLinux-4c2`), PR contra `fase-4c1-plugins`, com checklist `docs/testing/phase4c2-manual-checklist.md`; depois, a 4c.3.
4. Nova seção "Limitações conhecidas da Fase 4c.2", acima da da 4c.1:
   - Um `DISPLAY` definido mas inalcançável (ex.: `:0` sem servidor X) ainda aborta na Qt ("could not connect to display"); só a falta das variáveis é detectada.
   - Um helper que para de responder com programas rodando só é morto quando o mount dele some; se ele nunca desmontar, o mount fica até o usuário fechar os programas e usar `fusermount3 -uz` (ou "Unlock"), e o refresh fica adiado até lá.
   - "Executar" sem VFS com um helper travado ainda descarta o helper travado sem matá-lo (o destrutor espera até 5 s e o deixa rodando), como antes.
   - Valem as limitações das Fases 1 a 4c.1.
5. "Pendências por fase → Fase 2": risque os itens 1–5 e 7 (`~~…~~`) com "Resolvido na Fase 4c.2 (…)" no fim de cada um, como o item 6. "Fase 3 (sobras)": risque `Estado "não foi possível desmontar" nunca reconfere o mountinfo (refresh fica adiado até o próximo Run)` (só essa parte da frase; o resto do item continua). "Fase 4 (Ecossistema)": risque `MO2_VFS_TIMEOUT_MS também pode alongar o timeout: limitar a 30 s`.
6. "Decisões tomadas": uma linha `| 2026-10-20 | Fase 4c.2: escopo (decisão do usuário): pendências 1–5 e 7 da Fase 2, follow-ups de INI da 4c.1 e as vizinhas R1/R2; raízes deixadas montadas reconferidas a cada 5 s por `--cleanup` + `--list-mounts` assíncronos (nunca desmonta mount de helper vivo); helper travado com programa observado deixa a raiz como montada e só é morto quando o mount some; sem tela, mensagem e código 1 antes da `QApplication`; INIs de arquivos e da invalidação de BSA resolvidos ignorando maiúsculas no ponto central. Desenvolvida no worktree `../ModOrganizerLinux-4c2` |`.

- [ ] **Step 5: Commit e ponto de controle**

```bash
git add docs/testing/phase2-manual-checklist.md README.md docs/testing/phase4c2-manual-checklist.md STATE.md
git commit -m "docs: Phase 4c.2: Phase 2 checklist fixes, SSH note, 4c.2 checklist and state

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL"
```

Depois da revisão final do branch (e das correções dela), um commit vazio de ponto de controle:

```bash
git commit --allow-empty -m "ci: Phase 4c.2 checkpoint [windows-ci]

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL"
```

**Push e PR só com a autorização do usuário:** `git push -u origin fase-4c2-pendencias` e `gh pr create --base fase-4c1-plugins --head fase-4c2-pendencias --title "Fase 4c.2 — Pendências da Fase 2 e INIs"`, com corpo em português (resumo dos itens P1–P5, P7, R1, R2, I1, I2; testes; checklist; "empilhado sobre o PR #4") terminando com:

```
🤖 Generated with [Claude Code](https://claude.com/claude-code)

https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL
```

Conferir os dois CIs (`gh pr checks`): Linux (com `organizer_tests`, `game_gamebryo_tests`, smoke incluindo "without a display") e Windows (pelo `[windows-ci]`).

---

### Task 7: Teste manual com o usuário e fechamento

- [ ] **Step 1:** Passar ao usuário `docs/testing/phase4c2-manual-checklist.md`, o caminho do build (`install/` do worktree ou artefato do CI) e lembrar que o MO2 dele no checkout principal não deve estar aberto ao mesmo tempo que o de teste (instância única).
- [ ] **Step 2:** Corrigir o que o teste encontrar (cada correção com teste, no padrão das Tasks 1–5) e repetir os itens afetados.
- [ ] **Step 3:** Com o teste aprovado: status da spec para `aprovada; implementada`, STATE.md (status da 4c.2 ✅ teste manual, decisão com a data), commit `docs: Phase 4c.2: manual test passed`, e o merge segue a ordem do STATE.md (4b → 4c.1 → 4c.2, cada PR redirecionado ao `main` depois que o anterior entrar).

## Observações

- A Task 3 depende da 2 (usa `recheckLeftMounts()`, `keepLeftMounted()` com timer); as Tasks 1, 4 e 5 são independentes entre si. Ordem recomendada: 1, 2, 3, 4, 5, 6.
- `spawn()` não muda nesta fase (spec §2.3): ele ainda descarta um helper perdido sem matá-lo.
- Os testes novos do conector usam só helpers falsos (scripts `sh`), sem FUSE: rodam no CI sem `/dev/fuse`.
