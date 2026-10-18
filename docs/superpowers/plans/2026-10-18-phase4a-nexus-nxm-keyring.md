# Fase 4a — Nexus, `nxm://` e keyring — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** O MO2 no Linux lembra o login do Nexus/Steam no Secret Service (KWallet, GNOME Keyring) e é chamado pelo sistema para links `nxm://`, de modo que "Mod Manager Download" no Firefox baixa o mod no MO2.

**Architecture:** Três unidades novas em `deps/modorganizer/src/linux/`: `secretservice_linux` (wrapper C++ sem Qt da API síncrona da libsecret, numa biblioteca estática própria sem o cabeçalho pré-compilado), `secretstore_linux` (a política: Secret Service, com volta ao mapa em memória quando o serviço não existe) e `nxmhandler_linux` (arquivo `.desktop` + `xdg-mime`, rodando no host pela `vfsbridge` quando o MO2 está no distrobox). As funções de credenciais de `settingsutilities_linux.cpp` passam a usar o `secretstore`; as de registro do `nxmhandler.exe` em `settings.cpp` ganham versões Linux em `settings_linux.cpp`. O fluxo de download existente (`MOMultiProcess` → `downloadRequestedNXM`) não muda.

**Tech Stack:** C++23/Qt 6 (`QProcess`, `QSaveFile`, `QStandardPaths`); libsecret 0.21 (`libsecret-1`, API `*_sync`); GoogleTest; bash (smoke test); `xdg-mime` (xdg-utils), `dbus-run-session` e `gnome-keyring-daemon` (só no teste de integração).

**Spec:** `docs/superpowers/specs/2026-10-18-phase4a-nexus-nxm-keyring-design.md` (ler inteira antes de cada tarefa; §N são seções dela).

## Global Constraints

- Branch `fase-4a-nexus`. Toda mudança dentro de `deps/modorganizer` vai em commit próprio com o assunto prefixado por `deps/modorganizer: Linux:` (ex.: `deps/modorganizer: Linux: store credentials in the Secret Service`), como diz `deps/README.md`. Mudanças em `build/`, `tests/`, `docs/` e na raiz não levam prefixo de subtree (`build:`, `tests:`, `docs:`).
- O app continua compilando no Windows **contra o `uibase` upstream**. Código Linux do app em `src/linux/<nome>_linux.{h,cpp}`; em `settings.cpp` (arquivo compartilhado) as funções Windows-only ficam em `#ifdef _WIN32` e as versões Linux em `src/linux/settings_linux.cpp` (regras de `deps/modorganizer/src/linux/README.md`).
- Esquema libsecret: nome `org.modorganizer.ModOrganizer2`, flags `SECRET_SCHEMA_NONE`, um atributo string `key` com o valor `credentialName(key)` (`ModOrganizer2_APIKEY`, `ModOrganizer2_NEXUS_OAUTH_TOKENS`, `ModOrganizer2_steam_password`); coleção `SECRET_COLLECTION_DEFAULT`; rótulo `Mod Organizer 2: <nome>`; segredos UTF-8.
- Sem Secret Service: só memória, nada de segredo no disco; aviso por `log::warn` uma vez e um `QMessageBox::warning` único na primeira **gravação** em memória.
- Arquivo do handler: `$XDG_DATA_HOME/applications/modorganizer2-nxm-handler.desktop` (`QStandardPaths::GenericDataLocation` + `/applications`); MIME `x-scheme-handler/nxm`. Lançador, primeira regra que valer: `$APPIMAGE`; modo `Host` da `vfsbridge` com `CONTAINER_ID` → `distrobox enter <CONTAINER_ID> -- <executável>`; senão o executável. Modo `Host` sem `CONTAINER_ID`: registro recusado.
- Registro: sem forçar só grava/define quando não há handler (ou regrava o arquivo se o handler já é o nosso); forçando, sempre grava e define. Tudo assíncrono; falha sem forçar só vai para o log.
- Opção CMake `MO2_WITH_LIBSECRET` (Linux, padrão ON). OFF: compila e roda só com memória.
- Todo teste automático do app, o smoke e o e2e definem `MO2_VFS_BRIDGE=direct` (o CI roda num container). Nenhum teste toca o keyring, o `mimeapps.list` ou o `~/.local/share/applications` reais.
- Mensagens ao usuário no código em inglês. Formatação C++: `clang-format` do subtree (`deps/modorganizer/.clang-format`).
- Commits terminam com as linhas de atribuição da sessão; mensagens sem identificador de modelo.

## Ambiente de execução

O CI (GitHub Actions, container Fedora 44 de `build/Containerfile`) é a referência de "passou". No Bazzite, compilar e testar dentro da distrobox `mo2-run`, a partir da raiz do superprojeto:

- tudo do app: `distrobox enter mo2-run -- ./build.sh --only modorganizer --tests` (testes unitários, `ctest`, app e smoke test);
- só os testes unitários, depois de um configure: `distrobox enter mo2-run -- bash -c 'cmake --build build-out/modorganizer --target organizer_tests && build-out/modorganizer/tests/organizer_tests --gtest_filter="<Filtro>*"'`.

A distrobox precisa dos pacotes novos (Task 1): `distrobox enter mo2-run -- sudo dnf -y install libsecret-devel gnome-keyring dbus-daemon`.

## Review Focus

1. **Caminho do executável com espaço, `$`, `"`, `\`, `%` e não-ASCII** (`/home/u/Mod Organizer/Módulos/$x`) no `Exec=` do `.desktop`: o Plasma precisa receber o caminho intacto → testes `QuoteExecArgument` e `DesktopFileEscapesBackslashes` (Task 3).
2. **Segredos grandes com aspas e não-ASCII** (tokens OAuth em JSON, ~2 KB): ida e volta byte a byte pelo Secret Service → `SecretServiceIntegration.RoundTrip` usa um JSON de 4 KB com aspas e `çãø🔑` (Task 1).
3. **Carteira trancada ou prompt cancelado** (erro que não é "serviço ausente"): não pode jogar a sessão para a memória nem apagar nada; a próxima chamada tenta o Secret Service de novo → teste `FailedKeepsTheSecretService` (Task 2).
4. **Saída do `xdg-mime query` com quebra de linha ou espaços** (`modorganizer2-nxm-handler.desktop\n`): reconhecida como o nosso handler → caso com `\n` e espaços em `Decide` (Task 3).
5. **`xdg-mime` ausente** (CI, hosts mínimos) na abertura: nenhum diálogo, nada travado, só log; pelo botão, diálogo com a causa → teste `MissingXdgMimeFails` (Task 4) e o caminho sem forçar de `settings_linux.cpp` (Task 5).

---

### Task 1: `secretservice_linux` — libsecret sem Qt, pacotes e teste de integração

**Files:**
- Create: `deps/modorganizer/src/linux/secretservice_linux.h`
- Create: `deps/modorganizer/src/linux/secretservice_linux.cpp`
- Create: `deps/modorganizer/tests/test_secretservice.cpp`
- Create: `deps/modorganizer/tests/secret-service.sh`
- Modify: `deps/modorganizer/src/CMakeLists.txt` (opção, exclusão do alvo `organizer`, biblioteca `organizer_secretservice`)
- Modify: `deps/modorganizer/tests/CMakeLists.txt`
- Modify: `build/Containerfile`, `README.md` (lista de pacotes da distrobox `mo2-run`)

**Interfaces:**
- Produces (namespace `secretservice`, header sem Qt e sem GLib):
  - `enum class Status { Ok, NotFound, Unavailable, Failed };`
  - `struct Reply { Status status = Status::Failed; std::string value; std::string error; };`
  - `Reply lookup(const std::string& name);`
  - `Reply store(const std::string& name, const std::string& label, const std::string& value);`
  - `Reply clear(const std::string& name);`
  - Alvo CMake `organizer_secretservice` (só com `MO2_WITH_LIBSECRET`), variável de cache `MO2_WITH_LIBSECRET`.

- [ ] **Step 1: Pacotes no container e na distrobox**

Confirme qual pacote traz o `dbus-run-session` no Fedora 44:

Run: `distrobox enter mo2-run -- dnf provides /usr/bin/dbus-run-session`
Expected: `dbus-daemon-…fc44` (se for outro pacote, use o nome mostrado no lugar de `dbus-daemon` abaixo).

Em `build/Containerfile`, acrescente à linha de `gtest-devel fuse3 fuse3-devel clang-tools-extra`:

```dockerfile
        gtest-devel fuse3 fuse3-devel clang-tools-extra \
        libsecret-devel gnome-keyring dbus-daemon \
```

Em `README.md`, na lista do `dnf -y install` da distrobox `mo2-run` (rota `fedora:44`), acrescente `libsecret` (biblioteca de execução) ao fim da última linha:

```sh
         fmt-devel lz4-devel zlib-devel fuse3-devel qt6-qtwayland libsecret
```

Instale na distrobox local:

Run: `distrobox enter mo2-run -- sudo dnf -y install --setopt=install_weak_deps=False libsecret-devel gnome-keyring dbus-daemon`
Expected: pacotes instalados (ou "already installed").

- [ ] **Step 2: Escreva os testes que falham**

`deps/modorganizer/tests/test_secretservice.cpp`:

```cpp
#include "../src/linux/secretservice_linux.h"
#include "tempdir.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <string>
#include <unistd.h>

#ifdef MO2_WITH_LIBSECRET

namespace
{
// sets an environment variable for the lifetime of the object
struct ScopedEnv
{
  ScopedEnv(const char* name, const QByteArray& value)
      : m_name(name), m_had(qEnvironmentVariableIsSet(name)), m_old(qgetenv(name))
  {
    qputenv(name, value);
  }
  ~ScopedEnv()
  {
    if (m_had) {
      qputenv(m_name, m_old);
    } else {
      qunsetenv(m_name);
    }
  }
  const char* m_name;
  bool m_had;
  QByteArray m_old;
};
}  // namespace

// no session bus: the Secret Service is unavailable, not "failed" (spec §3.2)
TEST(SecretServiceUnavailable, NoSessionBus)
{
  TempDir runtime;
  const auto bus = "unix:path=" + (runtime.path() / "no-such-bus").string();
  ScopedEnv address("DBUS_SESSION_BUS_ADDRESS", QByteArray::fromStdString(bus));
  ScopedEnv xdg("XDG_RUNTIME_DIR", QByteArray::fromStdString(runtime.path().string()));
  ScopedEnv display("DISPLAY", "");

  const auto r = secretservice::lookup("ModOrganizer2_test");
  EXPECT_EQ(r.status, secretservice::Status::Unavailable) << r.error;
  EXPECT_FALSE(r.error.empty());
}

// against a real Secret Service: run by tests/secret-service.sh (ctest
// secret_service_integration), which starts a gnome-keyring-daemon of its own and sets
// MO2_TESTS_SECRET_SERVICE=1; skipped otherwise, so that the user's keyring is never
// touched
TEST(SecretServiceIntegration, RoundTrip)
{
  if (qgetenv("MO2_TESTS_SECRET_SERVICE") != "1") {
    GTEST_SKIP() << "run by tests/secret-service.sh";
  }

  const std::string name = "ModOrganizer2_test_" + std::to_string(::getpid());

  // like the OAuth tokens: JSON with quotes, plus non-ASCII, about 4 KB
  std::string value = R"({"access_token":"çãø🔑","refresh_token":")";
  value += std::string(4000, 'x');
  value += R"("})";

  auto r = secretservice::lookup(name);
  EXPECT_EQ(r.status, secretservice::Status::NotFound) << r.error;

  r = secretservice::store(name, "Mod Organizer 2: " + name, value);
  ASSERT_EQ(r.status, secretservice::Status::Ok) << r.error;

  r = secretservice::lookup(name);
  ASSERT_EQ(r.status, secretservice::Status::Ok) << r.error;
  EXPECT_EQ(r.value, value);

  // storing again replaces the value
  r = secretservice::store(name, "Mod Organizer 2: " + name, "second");
  ASSERT_EQ(r.status, secretservice::Status::Ok) << r.error;
  EXPECT_EQ(secretservice::lookup(name).value, "second");

  r = secretservice::clear(name);
  EXPECT_EQ(r.status, secretservice::Status::Ok) << r.error;
  EXPECT_EQ(secretservice::lookup(name).status, secretservice::Status::NotFound);

  // clearing what does not exist is not an error
  EXPECT_EQ(secretservice::clear(name).status, secretservice::Status::NotFound);
}

#endif  // MO2_WITH_LIBSECRET
```

`deps/modorganizer/tests/secret-service.sh` (depois de criar: `chmod +x deps/modorganizer/tests/secret-service.sh`):

```sh
#!/bin/sh
# Runs the Secret Service integration test of organizer_tests
# (SecretServiceIntegration.*, test_secretservice.cpp) against a gnome-keyring-daemon
# of its own, in a D-Bus session of its own, with a temporary HOME: the user's keyring
# is never touched. Exit 77 (ctest SKIP_RETURN_CODE) when the tools are missing.
#
#   tests/secret-service.sh <organizer_tests binary>
set -eu

TESTS="${1:?usage: $0 <organizer_tests binary>}"

if ! command -v dbus-run-session >/dev/null 2>&1 ||
    ! command -v gnome-keyring-daemon >/dev/null 2>&1; then
    echo "skipped: dbus-run-session or gnome-keyring-daemon not found"
    exit 77
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -m 700 "$work/runtime"

export HOME="$work" XDG_DATA_HOME="$work/data" XDG_CONFIG_HOME="$work/config" \
    XDG_RUNTIME_DIR="$work/runtime"
unset DISPLAY WAYLAND_DISPLAY

# --unlock reads the password of the new "login" keyring (the default collection)
# from stdin
dbus-run-session -- sh -c '
    printf "mo2-tests" | gnome-keyring-daemon --unlock --components=secrets >/dev/null
    MO2_TESTS_SECRET_SERVICE=1 exec "$0" --gtest_filter="SecretServiceIntegration.*"
' "$TESTS"
```

- [ ] **Step 3: CMake**

Em `deps/modorganizer/src/CMakeLists.txt`, logo depois do `option(MO2_WITH_LIBBSARCH …)`:

```cmake
# libsecret stores the Nexus and Steam logins in the Secret Service of the desktop
# (KWallet, GNOME Keyring); without it, they are kept in memory until Mod Organizer
# exits (spec of Phase 4a, section 3)
if (NOT WIN32)
	option(MO2_WITH_LIBSECRET "store logins in the Secret Service with libsecret" ON)
endif()
```

Na condição de exclusão do `foreach(_source IN LISTS _organizer_sources)` (ramo Linux), acrescente depois de `OR _relative STREQUAL "linux/posixspawn_linux.cpp"`:

```cmake
				OR _relative STREQUAL "linux/secretservice_linux.cpp"
```

Dentro do `if (NOT WIN32)` que cria as bibliotecas, logo depois do bloco de `organizer_posixspawn`:

```cmake
	# libsecret, without the precompiled header and without Qt: GLib's headers break
	# under Qt's `signals` macro (secretservice_linux.h has neither GLib nor Qt)
	if (MO2_WITH_LIBSECRET)
		find_package(PkgConfig REQUIRED)
		pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
		add_library(organizer_secretservice STATIC linux/secretservice_linux.cpp)
		mo2_configure_target(organizer_secretservice
			NO_SOURCES WARNINGS 4 TRANSLATIONS OFF AUTOMOC OFF)
		target_link_libraries(organizer_secretservice PRIVATE PkgConfig::LIBSECRET)
	endif()
```

Em `deps/modorganizer/tests/CMakeLists.txt`, no fim:

```cmake
# the Secret Service wrapper (test_secretservice.cpp); its integration test runs against
# a gnome-keyring-daemon of its own (secret-service.sh), 77 meaning skipped
if (TARGET organizer_secretservice)
	target_link_libraries(organizer_tests PRIVATE organizer_secretservice)
	target_compile_definitions(organizer_tests PRIVATE MO2_WITH_LIBSECRET)
	add_test(NAME secret_service_integration
		COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/secret-service.sh
			$<TARGET_FILE:organizer_tests>)
	set_tests_properties(secret_service_integration PROPERTIES SKIP_RETURN_CODE 77)
endif()
```

- [ ] **Step 4: Rode os testes para ver falhar**

Run: `distrobox enter mo2-run -- ./build.sh --only modorganizer --tests`
Expected: FAIL no configure do `organizer_tests`: `secretservice_linux.cpp` não existe (`Cannot find source file`).

- [ ] **Step 5: Implemente o wrapper**

`deps/modorganizer/src/linux/secretservice_linux.h`:

```cpp
#ifndef SECRETSERVICE_LINUX_H
#define SECRETSERVICE_LINUX_H

#include <string>

// The Secret Service of the desktop (KWallet, GNOME Keyring) through the synchronous
// API of libsecret (spec of Phase 4a, section 3.1). This header has neither Qt nor
// GLib: the implementation is built without the precompiled header of the application,
// since GLib's headers do not compile under Qt's `signals` macro. Values are UTF-8.
namespace secretservice
{
enum class Status
{
  Ok,
  // lookup: no such item; clear: nothing was removed
  NotFound,
  // no session bus, or no Secret Service on it
  Unavailable,
  // any other error (locked collection, dismissed prompt, error of the service)
  Failed
};

struct Reply
{
  Status status = Status::Failed;
  // lookup: the secret
  std::string value;
  // the message of the error, for Unavailable and Failed
  std::string error;
};

// items are found by the attribute `key` = name in the schema
// org.modorganizer.ModOrganizer2
Reply lookup(const std::string& name);

// in the default collection, replacing an existing item
Reply store(const std::string& name, const std::string& label, const std::string& value);

Reply clear(const std::string& name);
}  // namespace secretservice

#endif  // SECRETSERVICE_LINUX_H
```

`deps/modorganizer/src/linux/secretservice_linux.cpp`:

```cpp
// The Secret Service through libsecret (secretservice_linux.h). Built on its own,
// without the precompiled header and without Qt (see src/CMakeLists.txt).

#include "secretservice_linux.h"

#include <libsecret/secret.h>

namespace secretservice
{
namespace
{
const SecretSchema& schema()
{
  static const SecretSchema s = [] {
    SecretSchema r{};
    r.name          = "org.modorganizer.ModOrganizer2";
    r.flags         = SECRET_SCHEMA_NONE;
    r.attributes[0] = {"key", SECRET_SCHEMA_ATTRIBUTE_STRING};
    // attributes[1] stays zeroed: the end of the list
    return r;
  }();
  return s;
}

// Unavailable for the errors of reaching the service (no bus, nobody owns
// org.freedesktop.secrets): the caller falls back to memory for the session. Failed
// for everything else, which can go away (a locked wallet, a dismissed prompt, an
// error the service returned).
Status classify(const GError* e)
{
  if (e->domain == G_IO_ERROR) {
    switch (e->code) {
    case G_IO_ERROR_CANCELLED:
    case G_IO_ERROR_DBUS_ERROR:  // an unregistered error returned by the service
    case G_IO_ERROR_TIMED_OUT:
    case G_IO_ERROR_PERMISSION_DENIED:
      return Status::Failed;
    default:
      return Status::Unavailable;
    }
  }

  if (e->domain == G_SPAWN_ERROR) {
    return Status::Unavailable;
  }

  if (e->domain == G_DBUS_ERROR) {
    switch (e->code) {
    case G_DBUS_ERROR_SERVICE_UNKNOWN:
    case G_DBUS_ERROR_NAME_HAS_NO_OWNER:
    case G_DBUS_ERROR_NO_SERVER:
    case G_DBUS_ERROR_DISCONNECTED:
    case G_DBUS_ERROR_FILE_NOT_FOUND:
    case G_DBUS_ERROR_SPAWN_EXEC_FAILED:
    case G_DBUS_ERROR_SPAWN_FORK_FAILED:
    case G_DBUS_ERROR_SPAWN_CHILD_EXITED:
    case G_DBUS_ERROR_SPAWN_CHILD_SIGNALED:
    case G_DBUS_ERROR_SPAWN_FAILED:
    case G_DBUS_ERROR_SPAWN_SERVICE_INVALID:
    case G_DBUS_ERROR_SPAWN_SERVICE_NOT_FOUND:
    case G_DBUS_ERROR_SPAWN_FILE_INVALID:
      return Status::Unavailable;
    default:
      return Status::Failed;
    }
  }

  return Status::Failed;
}

// consumes `e`
Reply fromError(GError* e)
{
  Reply r;
  r.status = classify(e);
  r.error  = e->message ? e->message : "unknown error";
  g_error_free(e);
  return r;
}
}  // namespace

Reply lookup(const std::string& name)
{
  GError* e = nullptr;
  gchar* v =
      secret_password_lookup_sync(&schema(), nullptr, &e, "key", name.c_str(), nullptr);

  if (e) {
    if (v) {
      secret_password_free(v);
    }
    return fromError(e);
  }

  if (!v) {
    return {Status::NotFound, {}, {}};
  }

  Reply r{Status::Ok, v, {}};
  secret_password_free(v);
  return r;
}

Reply store(const std::string& name, const std::string& label, const std::string& value)
{
  GError* e     = nullptr;
  const auto ok = secret_password_store_sync(&schema(), SECRET_COLLECTION_DEFAULT,
                                             label.c_str(), value.c_str(), nullptr, &e,
                                             "key", name.c_str(), nullptr);

  if (e) {
    return fromError(e);
  }

  if (!ok) {
    return {Status::Failed, {}, "the Secret Service did not store the item"};
  }

  return {Status::Ok, {}, {}};
}

Reply clear(const std::string& name)
{
  GError* e = nullptr;
  const auto removed =
      secret_password_clear_sync(&schema(), nullptr, &e, "key", name.c_str(), nullptr);

  if (e) {
    return fromError(e);
  }

  return {removed ? Status::Ok : Status::NotFound, {}, {}};
}
}  // namespace secretservice
```

- [ ] **Step 6: Rode os testes**

Run: `distrobox enter mo2-run -- bash -c 'cmake --build build-out/modorganizer --target organizer_tests && build-out/modorganizer/tests/organizer_tests --gtest_filter="SecretService*" && ctest --test-dir build-out/modorganizer -R "secret_service_integration|SecretService" --output-on-failure'`
Expected: `SecretServiceUnavailable.NoSessionBus` PASS; `SecretServiceIntegration.RoundTrip` SKIPPED no binário direto; no `ctest`, `secret_service_integration` **Passed** (o `RoundTrip` rodou contra o `gnome-keyring-daemon` próprio: confira `[  PASSED  ] 1 test` na saída com `--output-on-failure -V` se preciso).

Se o `RoundTrip` falhar com erro de coleção padrão inexistente (`No such object` / `org.freedesktop.Secret.Error.NoSuchObject`), o `gnome-keyring-daemon --unlock` não criou o chaveiro `login` como padrão nesse ambiente: no script, antes do `exec`, crie-o com `secret-tool`/`busctl` ou troque o `--unlock` por `--start` precedido de `--unlock`; corrija o script, não o código.

- [ ] **Step 7: Formatação e commits**

Run: `distrobox enter mo2-run -- clang-format -i deps/modorganizer/src/linux/secretservice_linux.{h,cpp} deps/modorganizer/tests/test_secretservice.cpp`

```bash
git add build/Containerfile README.md
git commit -m "build: libsecret, gnome-keyring and dbus-daemon for the Secret Service (Phase 4a)"
git add deps/modorganizer/src/linux/secretservice_linux.h deps/modorganizer/src/linux/secretservice_linux.cpp \
    deps/modorganizer/src/CMakeLists.txt deps/modorganizer/tests/CMakeLists.txt \
    deps/modorganizer/tests/test_secretservice.cpp deps/modorganizer/tests/secret-service.sh
git commit -m "deps/modorganizer: Linux: Secret Service wrapper over libsecret"
```

---

### Task 2: `secretstore_linux` — política de armazenamento e credenciais do MO2

**Files:**
- Create: `deps/modorganizer/src/linux/secretstore_linux.h`
- Create: `deps/modorganizer/src/linux/secretstore_linux.cpp`
- Create: `deps/modorganizer/tests/test_secretstore.cpp`
- Modify: `deps/modorganizer/src/linux/settingsutilities_linux.cpp` (reescrito)
- Modify: `deps/modorganizer/src/CMakeLists.txt` (biblioteca `organizer_desktop`)
- Modify: `deps/modorganizer/tests/CMakeLists.txt`
- Modify: `deps/modorganizer/src/linux/README.md`

**Interfaces:**
- Consumes: `secretservice::{Status, Reply, lookup, store, clear}` (Task 1).
- Produces (namespace `secretstore`):
  - `class Backend { virtual secretservice::Reply lookup(const QString& name) = 0; virtual secretservice::Reply store(const QString& name, const QString& value) = 0; virtual secretservice::Reply clear(const QString& name) = 0; };`
  - `std::unique_ptr<Backend> libsecretBackend();` (nullptr sem `MO2_WITH_LIBSECRET`)
  - `class Store { explicit Store(std::unique_ptr<Backend>); QString get(const QString&); bool set(const QString&, const QString&); bool remove(const QString&); bool inMemory() const; bool takeMemoryWarning(); };`
  - `Store& instance();`
  - Alvo CMake `organizer_desktop` (estático, `PUBLIC` em `organizer_launch` e, com libsecret, em `organizer_secretservice` + definição `MO2_WITH_LIBSECRET`).

- [ ] **Step 1: Escreva os testes que falham**

`deps/modorganizer/tests/test_secretstore.cpp`:

```cpp
#include "../src/linux/secretstore_linux.h"

#include <gtest/gtest.h>

#include <map>

using secretservice::Reply;
using secretservice::Status;

namespace
{
// what the fake Secret Service holds and how it answers; the store owns the backend,
// the test keeps this
struct FakeState
{
  std::map<QString, QString> values;
  // anything but Ok: every call answers with this status
  Status forced = Status::Ok;
  int calls     = 0;
};

class FakeBackend : public secretstore::Backend
{
public:
  explicit FakeBackend(FakeState& s) : m_s(s) {}

  Reply lookup(const QString& name) override
  {
    ++m_s.calls;
    if (m_s.forced != Status::Ok) {
      return {m_s.forced, {}, "forced"};
    }
    const auto it = m_s.values.find(name);
    if (it == m_s.values.end()) {
      return {Status::NotFound, {}, {}};
    }
    return {Status::Ok, it->second.toStdString(), {}};
  }

  Reply store(const QString& name, const QString& value) override
  {
    ++m_s.calls;
    if (m_s.forced != Status::Ok) {
      return {m_s.forced, {}, "forced"};
    }
    m_s.values[name] = value;
    return {Status::Ok, {}, {}};
  }

  Reply clear(const QString& name) override
  {
    ++m_s.calls;
    if (m_s.forced != Status::Ok) {
      return {m_s.forced, {}, "forced"};
    }
    return {m_s.values.erase(name) ? Status::Ok : Status::NotFound, {}, {}};
  }

private:
  FakeState& m_s;
};

secretstore::Store makeStore(FakeState& s)
{
  return secretstore::Store(std::make_unique<FakeBackend>(s));
}
}  // namespace

TEST(SecretStore, RoundTripThroughTheBackend)
{
  FakeState s;
  auto store = makeStore(s);

  EXPECT_TRUE(store.set("ModOrganizer2_APIKEY", "çãø🔑"));
  EXPECT_EQ(s.values["ModOrganizer2_APIKEY"], "çãø🔑");
  EXPECT_EQ(store.get("ModOrganizer2_APIKEY"), "çãø🔑");
  EXPECT_TRUE(store.remove("ModOrganizer2_APIKEY"));
  EXPECT_TRUE(store.get("ModOrganizer2_APIKEY").isEmpty());
  EXPECT_FALSE(store.inMemory());
  EXPECT_FALSE(store.takeMemoryWarning());
}

TEST(SecretStore, RemovingWhatDoesNotExistIsNotAnError)
{
  FakeState s;
  auto store = makeStore(s);
  EXPECT_TRUE(store.remove("ModOrganizer2_APIKEY"));
}

// spec §3.2: no Secret Service, memory for the rest of the session, one warning on
// the first store
TEST(SecretStore, UnavailableSwitchesToMemoryForTheSession)
{
  FakeState s;
  s.forced   = Status::Unavailable;
  auto store = makeStore(s);

  EXPECT_TRUE(store.set("a", "1"));
  EXPECT_TRUE(store.inMemory());
  EXPECT_EQ(store.get("a"), "1");
  EXPECT_TRUE(store.takeMemoryWarning());
  EXPECT_FALSE(store.takeMemoryWarning());

  // the service is not tried again, even if it came back
  s.forced         = Status::Ok;
  const auto calls = s.calls;
  EXPECT_TRUE(store.set("b", "2"));
  EXPECT_EQ(store.get("b"), "2");
  EXPECT_TRUE(store.remove("a"));
  EXPECT_TRUE(store.get("a").isEmpty());
  EXPECT_EQ(s.calls, calls);
  EXPECT_TRUE(s.values.empty());

  // one warning per session
  EXPECT_FALSE(store.takeMemoryWarning());
}

// reading at startup only logs: the warning waits for the first store
TEST(SecretStore, UnavailableOnReadDoesNotWarn)
{
  FakeState s;
  s.forced   = Status::Unavailable;
  auto store = makeStore(s);

  EXPECT_TRUE(store.get("a").isEmpty());
  EXPECT_TRUE(store.inMemory());
  EXPECT_FALSE(store.takeMemoryWarning());

  EXPECT_TRUE(store.set("a", "1"));
  EXPECT_TRUE(store.takeMemoryWarning());
}

// a locked wallet or a dismissed prompt: the operation fails, nothing goes to memory and
// the next call tries the Secret Service again
TEST(SecretStore, FailedKeepsTheSecretService)
{
  FakeState s;
  s.values["a"] = "kept";
  s.forced      = Status::Failed;
  auto store    = makeStore(s);

  EXPECT_FALSE(store.set("a", "new"));
  EXPECT_TRUE(store.get("a").isEmpty());
  EXPECT_FALSE(store.remove("a"));
  EXPECT_FALSE(store.inMemory());
  EXPECT_FALSE(store.takeMemoryWarning());

  s.forced = Status::Ok;
  EXPECT_EQ(store.get("a"), "kept");
  EXPECT_TRUE(store.set("a", "new"));
  EXPECT_EQ(s.values["a"], "new");
}

// built without libsecret
TEST(SecretStore, NoBackendIsMemoryOnly)
{
  secretstore::Store store(nullptr);
  EXPECT_TRUE(store.inMemory());
  EXPECT_TRUE(store.get("a").isEmpty());
  EXPECT_FALSE(store.takeMemoryWarning());
  EXPECT_TRUE(store.set("a", "1"));
  EXPECT_EQ(store.get("a"), "1");
  EXPECT_TRUE(store.takeMemoryWarning());
  EXPECT_FALSE(store.takeMemoryWarning());
}
```

Em `deps/modorganizer/tests/CMakeLists.txt`, troque o `target_link_libraries(organizer_tests PRIVATE …)` existente para incluir `organizer_desktop` antes de `organizer_launch`:

```cmake
target_link_libraries(organizer_tests PRIVATE
	organizer_desktop organizer_launch organizer_archivetree organizer_env
	organizer_datatree)
```

e, no bloco `if (TARGET organizer_secretservice)` da Task 1, apague as duas linhas `target_link_libraries(organizer_tests PRIVATE organizer_secretservice)` e `target_compile_definitions(organizer_tests PRIVATE MO2_WITH_LIBSECRET)` (agora vêm de `organizer_desktop`, `PUBLIC`). Atualize o comentário do topo do arquivo para citar `organizer_desktop` (credenciais e handler `nxm`).

- [ ] **Step 2: Rode para ver falhar**

Run: `distrobox enter mo2-run -- bash -c 'cmake --build build-out/modorganizer --target organizer_tests'`
Expected: FAIL — alvo `organizer_desktop` desconhecido / `secretstore_linux.h` não existe.

- [ ] **Step 3: CMake de `organizer_desktop`**

Em `deps/modorganizer/src/CMakeLists.txt`, depois de `set(_organizer_launch_sources …)`:

```cmake
# the desktop integration of the Linux build: logins in the Secret Service
# (secretstore_linux.cpp, over secretservice_linux.cpp) and the nxm:// handler
# (nxmhandler_linux.cpp), spec of Phase 4a
set(_organizer_desktop_sources
	linux/secretstore_linux.cpp)
```

Na condição de exclusão do `foreach`, depois de `OR _relative IN_LIST _organizer_launch_sources`:

```cmake
				OR _relative IN_LIST _organizer_desktop_sources
```

No `if (NOT WIN32)` das bibliotecas, depois do bloco `if (MO2_WITH_LIBSECRET) … endif()` da Task 1 e antes de `target_link_libraries(organizer PRIVATE …)`:

```cmake
	add_library(organizer_desktop STATIC ${_organizer_desktop_sources})
	mo2_configure_target(organizer_desktop
		NO_SOURCES WARNINGS 4 TRANSLATIONS OFF AUTOMOC OFF)
	target_precompile_headers(organizer_desktop REUSE_FROM organizer_datatree)
	target_link_libraries(organizer_desktop PUBLIC organizer_launch)
	if (MO2_WITH_LIBSECRET)
		target_link_libraries(organizer_desktop PUBLIC organizer_secretservice)
		target_compile_definitions(organizer_desktop PUBLIC MO2_WITH_LIBSECRET)
	endif()
```

e troque a ligação do app:

```cmake
	target_link_libraries(organizer PRIVATE
		organizer_desktop organizer_launch organizer_archivetree organizer_env
		organizer_datatree)
```

Atualize o comentário grande antes de `set(_organizer_datatree_sources …)` para mencionar `organizer_desktop` (credenciais e handler `nxm`) na lista das bibliotecas que o app e os testes ligam.

- [ ] **Step 4: Implemente o `secretstore`**

`deps/modorganizer/src/linux/secretstore_linux.h`:

```cpp
#ifndef SECRETSTORE_LINUX_H
#define SECRETSTORE_LINUX_H

#include "secretservice_linux.h"

#include <QMutex>
#include <QString>

#include <map>
#include <memory>

// Where the logins of Mod Organizer (Nexus API key and OAuth tokens, Steam password)
// are kept on Linux: the Secret Service of the desktop, or memory when there is none
// (spec of Phase 4a, section 3.2). Used by the credential functions of
// settingsutilities_linux.cpp.
namespace secretstore
{
// the operations of the Secret Service; a fake replaces it in the tests
class Backend
{
public:
  virtual ~Backend() = default;

  virtual secretservice::Reply lookup(const QString& name)                       = 0;
  virtual secretservice::Reply store(const QString& name, const QString& value)  = 0;
  virtual secretservice::Reply clear(const QString& name)                        = 0;
};

// libsecret (secretservice_linux.h); nullptr when built without it
// (MO2_WITH_LIBSECRET off)
std::unique_ptr<Backend> libsecretBackend();

class Store
{
public:
  // a null backend: memory only
  explicit Store(std::unique_ptr<Backend> backend);

  // empty when there is no such value, or when the Secret Service failed (logged)
  QString get(const QString& name);

  // false when the Secret Service failed (logged); a value kept in memory is a success
  bool set(const QString& name, const QString& value);

  // removing what does not exist is a success
  bool remove(const QString& name);

  // true once the session uses memory: no backend, or the Secret Service answered
  // Unavailable; it is not tried again until Mod Organizer restarts
  bool inMemory() const;

  // true exactly once per session: after the first value stored in memory, when the
  // user has to be told that the login will not be kept
  bool takeMemoryWarning();

private:
  std::unique_ptr<Backend> m_backend;
  mutable QMutex m_mutex;
  bool m_memory;
  bool m_warningGiven   = false;
  bool m_warningPending = false;
  std::map<QString, QString> m_values;

  // switches to memory if `r` says the service is unavailable; true if it did
  bool fallBack(const secretservice::Reply& r);
};

// the store of the application, over libsecretBackend()
Store& instance();
}  // namespace secretstore

#endif  // SECRETSTORE_LINUX_H
```

`deps/modorganizer/src/linux/secretstore_linux.cpp`:

```cpp
// Logins of Mod Organizer in the Secret Service, or in memory when there is none
// (secretstore_linux.h, spec of Phase 4a, section 3).

#include "secretstore_linux.h"

#include <QMutexLocker>
#include <utility>

#include <uibase/log.h>

using namespace MOBase;
using secretservice::Reply;
using secretservice::Status;

namespace secretstore
{
namespace
{
#ifdef MO2_WITH_LIBSECRET
class LibsecretBackend : public Backend
{
public:
  Reply lookup(const QString& name) override
  {
    return secretservice::lookup(name.toStdString());
  }

  Reply store(const QString& name, const QString& value) override
  {
    return secretservice::store(name.toStdString(),
                                ("Mod Organizer 2: " + name).toStdString(),
                                value.toStdString());
  }

  Reply clear(const QString& name) override
  {
    return secretservice::clear(name.toStdString());
  }
};
#endif
}  // namespace

std::unique_ptr<Backend> libsecretBackend()
{
#ifdef MO2_WITH_LIBSECRET
  return std::make_unique<LibsecretBackend>();
#else
  return nullptr;
#endif
}

Store::Store(std::unique_ptr<Backend> backend)
    : m_backend(std::move(backend)), m_memory(m_backend == nullptr)
{
  if (m_memory) {
    log::warn("built without a secret store: logins are kept in memory until Mod "
              "Organizer exits");
  }
}

bool Store::fallBack(const Reply& r)
{
  if (r.status != Status::Unavailable) {
    return false;
  }

  m_memory = true;
  log::warn("the Secret Service is not available ({}): logins are kept in memory "
            "until Mod Organizer exits",
            r.error);

  return true;
}

QString Store::get(const QString& name)
{
  QMutexLocker lock(&m_mutex);

  if (!m_memory) {
    const auto r = m_backend->lookup(name);
    switch (r.status) {
    case Status::Ok:
      return QString::fromStdString(r.value);
    case Status::NotFound:
      return {};
    case Status::Failed:
      log::error("cannot read {} from the Secret Service: {}", name, r.error);
      return {};
    case Status::Unavailable:
      fallBack(r);
      break;
    }
  }

  const auto it = m_values.find(name);
  return it == m_values.end() ? QString() : it->second;
}

bool Store::set(const QString& name, const QString& value)
{
  QMutexLocker lock(&m_mutex);

  if (!m_memory) {
    const auto r = m_backend->store(name, value);
    if (r.status == Status::Ok) {
      log::debug("stored {} in the Secret Service", name);
      return true;
    }
    if (!fallBack(r)) {
      log::error("cannot store {} in the Secret Service: {}", name, r.error);
      return false;
    }
  }

  m_values[name] = value;
  log::debug("set credential {} (kept in memory until Mod Organizer exits)", name);

  if (!std::exchange(m_warningGiven, true)) {
    m_warningPending = true;
  }

  return true;
}

bool Store::remove(const QString& name)
{
  QMutexLocker lock(&m_mutex);

  if (!m_memory) {
    const auto r = m_backend->clear(name);
    if (r.status == Status::Ok || r.status == Status::NotFound) {
      if (r.status == Status::Ok) {
        log::debug("deleted {} from the Secret Service", name);
      }
      return true;
    }
    if (!fallBack(r)) {
      log::error("cannot delete {} from the Secret Service: {}", name, r.error);
      return false;
    }
  }

  m_values.erase(name);
  return true;
}

bool Store::inMemory() const
{
  QMutexLocker lock(&m_mutex);
  return m_memory;
}

bool Store::takeMemoryWarning()
{
  QMutexLocker lock(&m_mutex);
  return std::exchange(m_warningPending, false);
}

Store& instance()
{
  static Store store(libsecretBackend());
  return store;
}
}  // namespace secretstore
```


- [ ] **Step 5: Rode os testes**

Run: `distrobox enter mo2-run -- bash -c 'cmake --build build-out/modorganizer --target organizer_tests && build-out/modorganizer/tests/organizer_tests --gtest_filter="SecretStore*:SecretService*"'`
Expected: os 6 testes `SecretStore.*` e `SecretServiceUnavailable.NoSessionBus` PASS; `SecretServiceIntegration.RoundTrip` SKIPPED.

- [ ] **Step 6: Reescreva `settingsutilities_linux.cpp`**

Substitua o arquivo inteiro `deps/modorganizer/src/linux/settingsutilities_linux.cpp` por:

```cpp
// Linux implementation of the credential functions of settingsutilities.cpp (the
// Windows ones use the Windows Credential Manager): the Secret Service of the desktop
// (KWallet, GNOME Keyring), or memory when there is none, see secretstore_linux.h and
// the spec of Phase 4a, section 3.

#include "../settingsutilities.h"
#include "secretstore_linux.h"

#include <QApplication>
#include <QMessageBox>
#include <QMetaObject>
#include <QThread>

#include <cerrno>

// in settingsutilities.cpp
QString credentialName(const QString& key);

namespace
{
// once per session, after the first login kept in memory only
void warnLoginNotKept()
{
  if (!qApp) {
    return;
  }

  const auto show = [] {
    QMessageBox::warning(
        nullptr, QObject::tr("Login not saved"),
        QObject::tr("No secret store (KWallet, GNOME Keyring) is available: Mod "
                    "Organizer will forget this login when it closes."));
  };

  if (QThread::currentThread() == qApp->thread()) {
    show();
  } else {
    QMetaObject::invokeMethod(qApp, show, Qt::QueuedConnection);
  }
}
}  // namespace

bool deleteWindowsCredential(const QString& key)
{
  return secretstore::instance().remove(credentialName(key));
}

bool addWindowsCredential(const QString& key, const QString& data)
{
  auto& store   = secretstore::instance();
  const auto ok = store.set(credentialName(key), data);

  if (store.takeMemoryWarning()) {
    warnLoginNotKept();
  }

  return ok;
}

QString getWindowsCredential(const QString& key)
{
  const auto value = secretstore::instance().get(credentialName(key));

  if (value.isEmpty()) {
    errno = ENOENT;
  }

  return value;
}
```

Em `deps/modorganizer/src/linux/README.md`, troque `` `settingsutilities.cpp` → `settingsutilities_linux.cpp` (credentials, kept in memory only until there is a secret store) `` por `` `settingsutilities.cpp` → `settingsutilities_linux.cpp` (credentials in the Secret Service through `secretstore_linux.cpp`, over libsecret in `secretservice_linux.cpp`; in memory when there is no Secret Service) ``.

- [ ] **Step 7: Compile o app e rode a suíte**

Run: `distrobox enter mo2-run -- ./build.sh --only modorganizer --tests`
Expected: `organizer_tests` verdes (incluindo `secret_service_integration` Passed), app instalado, smoke test `smoke test passed`.

- [ ] **Step 8: Formatação e commit**

Run: `distrobox enter mo2-run -- clang-format -i deps/modorganizer/src/linux/secretstore_linux.{h,cpp} deps/modorganizer/src/linux/settingsutilities_linux.cpp deps/modorganizer/tests/test_secretstore.cpp`

```bash
git add deps/modorganizer/src/linux/secretstore_linux.h deps/modorganizer/src/linux/secretstore_linux.cpp \
    deps/modorganizer/src/linux/settingsutilities_linux.cpp deps/modorganizer/src/linux/README.md \
    deps/modorganizer/src/CMakeLists.txt deps/modorganizer/tests/CMakeLists.txt \
    deps/modorganizer/tests/test_secretstore.cpp
git commit -m "deps/modorganizer: Linux: store credentials in the Secret Service"
```

---

### Task 3: `nxmhandler_linux` — `.desktop`, lançador, decisão e comandos

**Files:**
- Create: `deps/modorganizer/src/linux/nxmhandler_linux.h`
- Create: `deps/modorganizer/src/linux/nxmhandler_linux.cpp`
- Create: `deps/modorganizer/tests/test_nxmhandler.cpp`
- Modify: `deps/modorganizer/src/CMakeLists.txt` (`_organizer_desktop_sources`)

**Interfaces:**
- Consumes: `vfsbridge::mode(const QString& rootDir = "/")`, `vfsbridge::Mode::{Direct, Host}`, `vfsbridge::Command{QString program; QStringList arguments;}`, `vfsbridge::helperCommand(const QString& binary, const QStringList& args, const QString& rootDir = "/")` (lança `std::runtime_error` no modo `Host` sem `distrobox-host-exec`), `vfsbridge::hostCanonicalPath(const QString&, const QString& rootDir = "/")` — de `src/linux/vfsbridge_linux.h`.
- Produces (namespace `nxmhandler`):
  - `inline constexpr char DesktopFileName[] = "modorganizer2-nxm-handler.desktop";`
  - `inline constexpr char MimeType[] = "x-scheme-handler/nxm";`
  - `struct Environment { QString appImage; QString containerId; bool inContainer = false; QString executable; };`
  - `Environment currentEnvironment();`
  - `QString quoteExecArgument(const QString& arg);`
  - `QStringList launcher(const Environment& env);` (lança `std::runtime_error`)
  - `QString desktopFileContent(const Environment& env);` (lança `std::runtime_error`)
  - `enum class Action { None, WriteFile, WriteFileAndSetDefault };`
  - `Action decide(const QString& currentDefault, bool force);`
  - `QString applicationsDir();`, `QString desktopFilePath();`
  - `vfsbridge::Command queryCommand();`, `vfsbridge::Command setDefaultCommand();`, `vfsbridge::Command updateDatabaseCommand(const QString& dir);`

- [ ] **Step 1: Escreva os testes que falham**

`deps/modorganizer/tests/test_nxmhandler.cpp`:

```cpp
#include "../src/linux/nxmhandler_linux.h"
#include "tempdir.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
using nxmhandler::Action;
using nxmhandler::Environment;

namespace
{
// sets (or, with nullptr, unsets) an environment variable for the lifetime of the
// object
struct ScopedEnv
{
  ScopedEnv(const char* name, const char* value)
      : m_name(name), m_had(qEnvironmentVariableIsSet(name)), m_old(qgetenv(name))
  {
    if (value) {
      qputenv(name, value);
    } else {
      qunsetenv(name);
    }
  }
  ~ScopedEnv()
  {
    if (m_had) {
      qputenv(m_name, m_old);
    } else {
      qunsetenv(m_name);
    }
  }
  const char* m_name;
  bool m_had;
  QByteArray m_old;
};

Environment direct(const QString& exe)
{
  Environment e;
  e.executable = exe;
  return e;
}
}  // namespace

TEST(NxmHandler, QuoteExecArgument)
{
  using nxmhandler::quoteExecArgument;
  EXPECT_EQ(quoteExecArgument("/opt/mo2/bin/ModOrganizer"), "/opt/mo2/bin/ModOrganizer");
  EXPECT_EQ(quoteExecArgument("/home/u/Mod Organizer/ModOrganizer"),
            "\"/home/u/Mod Organizer/ModOrganizer\"");
  EXPECT_EQ(quoteExecArgument("/x/a$b\"c\\d`e"), "\"/x/a\\$b\\\"c\\\\d\\`e\"");
  EXPECT_EQ(quoteExecArgument("/x/100%"), "/x/100%%");
  EXPECT_EQ(quoteExecArgument("/x/Módulos/ModOrganizer"), "/x/Módulos/ModOrganizer");
  EXPECT_EQ(quoteExecArgument(""), "\"\"");
}

TEST(NxmHandler, LauncherDirect)
{
  EXPECT_EQ(nxmhandler::launcher(direct("/opt/mo2/bin/ModOrganizer")),
            QStringList{"/opt/mo2/bin/ModOrganizer"});
}

TEST(NxmHandler, LauncherDistrobox)
{
  Environment e = direct("/home/u/Mod Organizer/bin/ModOrganizer");
  e.inContainer = true;
  e.containerId = "mo2-run";
  EXPECT_EQ(nxmhandler::launcher(e),
            (QStringList{"distrobox", "enter", "mo2-run", "--",
                         "/home/u/Mod Organizer/bin/ModOrganizer"}));
}

TEST(NxmHandler, LauncherAppImageWins)
{
  Environment e = direct("/tmp/.mount_x/usr/bin/ModOrganizer");
  e.inContainer = true;
  e.containerId = "mo2-run";
  e.appImage    = "/home/u/Apps/ModOrganizer.AppImage";
  EXPECT_EQ(nxmhandler::launcher(e), QStringList{"/home/u/Apps/ModOrganizer.AppImage"});
}

TEST(NxmHandler, ContainerWithoutIdIsRefused)
{
  Environment e = direct("/opt/mo2/bin/ModOrganizer");
  e.inContainer = true;
  EXPECT_THROW(nxmhandler::launcher(e), std::runtime_error);
  EXPECT_THROW(nxmhandler::desktopFileContent(e), std::runtime_error);
}

TEST(NxmHandler, DesktopFileContent)
{
  Environment e = direct("/home/u/Mod Organizer/bin/ModOrganizer");
  e.inContainer = true;
  e.containerId = "mo2-run";

  EXPECT_EQ(nxmhandler::desktopFileContent(e),
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=Mod Organizer 2 (nxm links)\n"
            "Comment=Download Nexus Mods links with Mod Organizer 2\n"
            "Exec=distrobox enter mo2-run -- \"/home/u/Mod Organizer/bin/ModOrganizer\" "
            "%u\n"
            "Terminal=false\n"
            "NoDisplay=true\n"
            "MimeType=x-scheme-handler/nxm;\n");
}

// the string escapes of the Desktop Entry Specification apply on top of the quoting of
// Exec: a backslash of the quoted argument is written twice
TEST(NxmHandler, DesktopFileEscapesBackslashes)
{
  const auto content = nxmhandler::desktopFileContent(direct("/x/a$b/ModOrganizer"));
  EXPECT_TRUE(content.contains("\nExec=\"/x/a\\\\$b/ModOrganizer\" %u\n")) << content.toStdString();
}

TEST(NxmHandler, Decide)
{
  using nxmhandler::decide;
  EXPECT_EQ(decide("", false), Action::WriteFileAndSetDefault);
  EXPECT_EQ(decide("  \n", false), Action::WriteFileAndSetDefault);
  EXPECT_EQ(decide("modorganizer2-nxm-handler.desktop", false), Action::WriteFile);
  EXPECT_EQ(decide(" modorganizer2-nxm-handler.desktop\n", false), Action::WriteFile);
  EXPECT_EQ(decide("vortex-nxm.desktop", false), Action::None);

  EXPECT_EQ(decide("", true), Action::WriteFileAndSetDefault);
  EXPECT_EQ(decide("modorganizer2-nxm-handler.desktop", true),
            Action::WriteFileAndSetDefault);
  EXPECT_EQ(decide("vortex-nxm.desktop", true), Action::WriteFileAndSetDefault);
}

TEST(NxmHandler, Paths)
{
  TempDir data;
  ScopedEnv xdg("XDG_DATA_HOME", data.path().c_str());
  const auto dir = QString::fromStdString(data.path().string()) + "/applications";
  EXPECT_EQ(nxmhandler::applicationsDir(), dir);
  EXPECT_EQ(nxmhandler::desktopFilePath(), dir + "/modorganizer2-nxm-handler.desktop");
}

TEST(NxmHandler, CommandsDirect)
{
  ScopedEnv bridge("MO2_VFS_BRIDGE", "direct");

  auto c = nxmhandler::queryCommand();
  EXPECT_EQ(c.program, "xdg-mime");
  EXPECT_EQ(c.arguments, (QStringList{"query", "default", "x-scheme-handler/nxm"}));

  c = nxmhandler::setDefaultCommand();
  EXPECT_EQ(c.program, "xdg-mime");
  EXPECT_EQ(c.arguments, (QStringList{"default", "modorganizer2-nxm-handler.desktop",
                                      "x-scheme-handler/nxm"}));

  c = nxmhandler::updateDatabaseCommand("/home/u/.local/share/applications");
  EXPECT_EQ(c.program, "update-desktop-database");
  EXPECT_EQ(c.arguments, QStringList{"/home/u/.local/share/applications"});
}

TEST(NxmHandler, CommandsHost)
{
  TempDir bin;
  const auto hostExec = bin.path() / "distrobox-host-exec";
  {
    std::ofstream(hostExec) << "#!/bin/sh\nexec \"$@\"\n";
  }
  fs::permissions(hostExec, fs::perms::owner_all);

  ScopedEnv bridge("MO2_VFS_BRIDGE", "host");
  ScopedEnv path("PATH", bin.path().c_str());

  const auto c = nxmhandler::queryCommand();
  EXPECT_EQ(c.program, QString::fromStdString(hostExec.string()));
  EXPECT_EQ(c.arguments,
            (QStringList{"xdg-mime", "query", "default", "x-scheme-handler/nxm"}));
}

TEST(NxmHandler, CurrentEnvironment)
{
  ScopedEnv bridge("MO2_VFS_BRIDGE", "host");
  ScopedEnv appImage("APPIMAGE", "/a/b.AppImage");
  ScopedEnv container("CONTAINER_ID", "box");

  const auto e = nxmhandler::currentEnvironment();
  EXPECT_EQ(e.appImage, "/a/b.AppImage");
  EXPECT_EQ(e.containerId, "box");
  EXPECT_TRUE(e.inContainer);
  EXPECT_FALSE(e.executable.isEmpty());
}
```

Em `deps/modorganizer/src/CMakeLists.txt`, acrescente o arquivo à lista:

```cmake
set(_organizer_desktop_sources
	linux/nxmhandler_linux.cpp
	linux/secretstore_linux.cpp)
```

- [ ] **Step 2: Rode para ver falhar**

Run: `distrobox enter mo2-run -- bash -c 'cmake --build build-out/modorganizer --target organizer_tests'`
Expected: FAIL — `nxmhandler_linux.h` / `nxmhandler_linux.cpp` não existem.

- [ ] **Step 3: Implemente as partes puras**

`deps/modorganizer/src/linux/nxmhandler_linux.h`:

```cpp
#ifndef NXMHANDLER_LINUX_H
#define NXMHANDLER_LINUX_H

#include "vfsbridge_linux.h"

#include <QString>
#include <QStringList>

#include <functional>

// Mod Organizer as the handler of nxm:// links on Linux: a desktop file that runs it
// with the link, made the default handler of x-scheme-handler/nxm with xdg-mime, run on
// the host through vfsbridge when Mod Organizer runs in a container (spec of Phase 4a,
// section 4). Replaces nxmhandler.exe of Windows.
namespace nxmhandler
{
// the name of the desktop file, which is also how xdg-mime names the handler
inline constexpr char DesktopFileName[] = "modorganizer2-nxm-handler.desktop";
inline constexpr char MimeType[]        = "x-scheme-handler/nxm";

// what decides how the desktop file starts Mod Organizer
struct Environment
{
  // $APPIMAGE (Phase 5)
  QString appImage;
  // $CONTAINER_ID, set by distrobox
  QString containerId;
  // vfsbridge::mode() is Host
  bool inContainer = false;
  // QCoreApplication::applicationFilePath()
  QString executable;
};

Environment currentEnvironment();

// one argument of an Exec line, quoted by the rules of the Desktop Entry Specification
// (in double quotes when it has a reserved character, with ", `, $ and \ escaped; %
// doubled); the string escapes of the file are applied by desktopFileContent()
QString quoteExecArgument(const QString& arg);

// the program and arguments that start Mod Organizer, without the link: the AppImage,
// `distrobox enter <container> -- <executable>` in a container, or the executable;
// throws std::runtime_error in a container that is not a distrobox (no CONTAINER_ID)
QStringList launcher(const Environment& env);

// the whole desktop file; throws like launcher()
QString desktopFileContent(const Environment& env);

enum class Action
{
  None,
  WriteFile,
  WriteFileAndSetDefault
};

// what to do given the current default handler (the output of `xdg-mime query
// default`, trimmed here): table of section 4.2 of the spec
Action decide(const QString& currentDefault, bool force);

// $XDG_DATA_HOME/applications and the desktop file in it
QString applicationsDir();
QString desktopFilePath();

// `xdg-mime query default x-scheme-handler/nxm`, `xdg-mime default <file>
// x-scheme-handler/nxm` and `update-desktop-database <dir>`, through
// vfsbridge::helperCommand() (on the host in a container; throws like it)
vfsbridge::Command queryCommand();
vfsbridge::Command setDefaultCommand();
vfsbridge::Command updateDatabaseCommand(const QString& dir);
}  // namespace nxmhandler

#endif  // NXMHANDLER_LINUX_H
```

`deps/modorganizer/src/linux/nxmhandler_linux.cpp`:

```cpp
// Mod Organizer as the handler of nxm:// links (nxmhandler_linux.h, spec of Phase 4a,
// section 4).

#include "nxmhandler_linux.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <stdexcept>

namespace nxmhandler
{
Environment currentEnvironment()
{
  Environment e;
  e.appImage    = qEnvironmentVariable("APPIMAGE");
  e.containerId = qEnvironmentVariable("CONTAINER_ID");
  e.inContainer = vfsbridge::mode() == vfsbridge::Mode::Host;
  e.executable  = QCoreApplication::applicationFilePath();
  return e;
}

QString quoteExecArgument(const QString& arg)
{
  static const QString reserved = QStringLiteral(" \t\n\"'\\><~|&;$*?#()`");

  QString escaped;
  bool quote = arg.isEmpty();

  for (const QChar c : arg) {
    if (reserved.contains(c)) {
      quote = true;
    }
    if (c == '"' || c == '`' || c == '$' || c == '\\') {
      escaped += '\\';
    }
    escaped += c;
  }

  escaped.replace("%", "%%");

  return quote ? '"' + escaped + '"' : escaped;
}

QStringList launcher(const Environment& env)
{
  if (!env.appImage.isEmpty()) {
    return {env.appImage};
  }

  if (env.inContainer) {
    if (env.containerId.isEmpty()) {
      throw std::runtime_error(
          "cannot register the nxm handler from this container: CONTAINER_ID is not "
          "set (only distrobox is supported)");
    }
    return {"distrobox", "enter", env.containerId, "--", env.executable};
  }

  return {env.executable};
}

QString desktopFileContent(const Environment& env)
{
  QStringList quoted;
  for (const auto& a : launcher(env)) {
    quoted.append(quoteExecArgument(a));
  }

  // string escapes of the file (applied before the quoting rules when read)
  QString exec = quoted.join(' ') + " %u";
  exec.replace("\\", "\\\\")
      .replace("\n", "\\n")
      .replace("\t", "\\t")
      .replace("\r", "\\r");

  return QStringLiteral("[Desktop Entry]\n"
                        "Type=Application\n"
                        "Name=Mod Organizer 2 (nxm links)\n"
                        "Comment=Download Nexus Mods links with Mod Organizer 2\n"
                        "Exec=%1\n"
                        "Terminal=false\n"
                        "NoDisplay=true\n"
                        "MimeType=%2;\n")
      .arg(exec, QString::fromLatin1(MimeType));
}

Action decide(const QString& currentDefault, bool force)
{
  if (force) {
    return Action::WriteFileAndSetDefault;
  }

  const auto current = currentDefault.trimmed();

  if (current.isEmpty()) {
    return Action::WriteFileAndSetDefault;
  }

  if (current == QLatin1String(DesktopFileName)) {
    return Action::WriteFile;
  }

  return Action::None;
}

QString applicationsDir()
{
  return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
         "/applications";
}

QString desktopFilePath()
{
  return applicationsDir() + "/" + QLatin1String(DesktopFileName);
}

vfsbridge::Command queryCommand()
{
  return vfsbridge::helperCommand("xdg-mime",
                                  {"query", "default", QString::fromLatin1(MimeType)});
}

vfsbridge::Command setDefaultCommand()
{
  return vfsbridge::helperCommand("xdg-mime", {"default", QString::fromLatin1(DesktopFileName),
                                               QString::fromLatin1(MimeType)});
}

vfsbridge::Command updateDatabaseCommand(const QString& dir)
{
  return vfsbridge::helperCommand("update-desktop-database",
                                  {vfsbridge::hostCanonicalPath(dir)});
}
}  // namespace nxmhandler
```

Nota: `hostCanonicalPath` devolve o caminho sem mudança quando ele não existe (caso do teste `CommandsDirect`) e, no modo `Direct`, o caminho canônico; o teste usa um caminho inexistente, então o esperado é o próprio argumento.

- [ ] **Step 4: Rode os testes**

Run: `distrobox enter mo2-run -- bash -c 'cmake --build build-out/modorganizer --target organizer_tests && build-out/modorganizer/tests/organizer_tests --gtest_filter="NxmHandler.*"'`
Expected: os 12 testes `NxmHandler.*` PASS.

- [ ] **Step 5: Formatação e commit**

Run: `distrobox enter mo2-run -- clang-format -i deps/modorganizer/src/linux/nxmhandler_linux.{h,cpp} deps/modorganizer/tests/test_nxmhandler.cpp`

```bash
git add deps/modorganizer/src/linux/nxmhandler_linux.h deps/modorganizer/src/linux/nxmhandler_linux.cpp \
    deps/modorganizer/src/CMakeLists.txt deps/modorganizer/tests/test_nxmhandler.cpp
git commit -m "deps/modorganizer: Linux: desktop file and decision of the nxm handler"
```

---

### Task 4: `nxmhandler_linux` — registro assíncrono e `dump()`

**Files:**
- Modify: `deps/modorganizer/src/linux/nxmhandler_linux.h`
- Modify: `deps/modorganizer/src/linux/nxmhandler_linux.cpp`
- Modify: `deps/modorganizer/tests/test_nxmhandler.cpp`

**Interfaces:**
- Consumes: tudo o que a Task 3 produz.
- Produces:
  - `using Done = std::function<void(bool ok, const QString& message)>;`
  - `void registerHandler(bool force, Done done);` — sempre assíncrono: `done` é chamado depois, na thread do `QCoreApplication`, nunca dentro da própria chamada. `ok` falso só por erro (comando ausente, saída diferente de 0, arquivo não gravado, container sem `CONTAINER_ID`); `ok` verdadeiro também quando nada foi mudado (handler de outro programa, sem forçar).
  - `void dump();` — síncrono (espera até 5 s), só `log::debug`.

- [ ] **Step 1: Escreva os testes que falham**

Acrescente a `deps/modorganizer/tests/test_nxmhandler.cpp` (os includes `<QEventLoop>`, `<QFile>`, `<QTimer>` e `<optional>` no topo):

```cpp
namespace
{
// a fake xdg-mime and update-desktop-database in a directory put first in the PATH:
// they append their arguments to calls.log; `xdg-mime query` prints the content of
// default.txt, `xdg-mime default` writes it
struct FakeTools
{
  TempDir dir;
  fs::path bin, data, log, defaultFile;

  FakeTools()
      : bin(dir.path() / "bin"), data(dir.path() / "data"), log(dir.path() / "calls.log"),
        defaultFile(dir.path() / "default.txt")
  {
    fs::create_directories(bin);
    script("xdg-mime", "echo \"xdg-mime $*\" >> '" + log.string() +
                           "'\n"
                           "case \"$1\" in\n"
                           "  query) cat '" +
                           defaultFile.string() +
                           "' 2>/dev/null; exit 0 ;;\n"
                           "  default) echo \"$2\" > '" +
                           defaultFile.string() +
                           "' ;;\n"
                           "esac\n");
    script("update-desktop-database",
           "echo \"update-desktop-database $*\" >> '" + log.string() + "'\n");
  }

  void script(const std::string& name, const std::string& body)
  {
    const auto p = bin / name;
    std::ofstream(p) << "#!/bin/sh\n" << body;
    fs::permissions(p, fs::perms::owner_all);
  }

  void setDefault(const std::string& handler)
  {
    std::ofstream(defaultFile) << handler << "\n";
  }

  QString calls() const
  {
    QFile f(QString::fromStdString(log.string()));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
  }

  QString desktopFile() const
  {
    QFile f(QString::fromStdString(
        (data / "applications" / "modorganizer2-nxm-handler.desktop").string()));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
  }

  std::string pathWithSh() const { return bin.string() + ":/usr/bin:/bin"; }
};

// runs registerHandler() and its event loop until `done` (10 s at most)
std::pair<bool, QString> registerAndWait(bool force)
{
  std::optional<std::pair<bool, QString>> r;
  bool synchronous = true;
  QEventLoop loop;

  nxmhandler::registerHandler(force, [&](bool ok, const QString& message) {
    r = {ok, message};
    loop.quit();
  });

  // the contract: never called from inside registerHandler()
  synchronous = r.has_value();
  EXPECT_FALSE(synchronous);

  if (!r) {
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
  }

  return r.value_or(std::pair<bool, QString>{false, "timeout"});
}
}  // namespace

TEST(NxmHandlerRegistration, NoHandlerWritesTheFileAndSetsTheDefault)
{
  FakeTools t;
  ScopedEnv bridge("MO2_VFS_BRIDGE", "direct");
  ScopedEnv appImage("APPIMAGE", nullptr);
  ScopedEnv path("PATH", t.pathWithSh().c_str());
  ScopedEnv xdg("XDG_DATA_HOME", t.data.c_str());

  const auto [ok, message] = registerAndWait(false);
  ASSERT_TRUE(ok) << message.toStdString();

  EXPECT_TRUE(t.desktopFile().contains(
      "Exec=" + nxmhandler::quoteExecArgument(QCoreApplication::applicationFilePath())))
      << t.desktopFile().toStdString();
  EXPECT_TRUE(t.calls().contains("xdg-mime query default x-scheme-handler/nxm"));
  EXPECT_TRUE(t.calls().contains(
      "xdg-mime default modorganizer2-nxm-handler.desktop x-scheme-handler/nxm"));
  EXPECT_TRUE(t.calls().contains("update-desktop-database "));
}

TEST(NxmHandlerRegistration, OurHandlerOnlyRewritesTheFile)
{
  FakeTools t;
  ScopedEnv bridge("MO2_VFS_BRIDGE", "direct");
  ScopedEnv appImage("APPIMAGE", nullptr);
  ScopedEnv path("PATH", t.pathWithSh().c_str());
  ScopedEnv xdg("XDG_DATA_HOME", t.data.c_str());

  t.setDefault("modorganizer2-nxm-handler.desktop");
  fs::create_directories(t.data / "applications");
  std::ofstream(t.data / "applications" / "modorganizer2-nxm-handler.desktop")
      << "old\n";

  const auto [ok, message] = registerAndWait(false);
  ASSERT_TRUE(ok) << message.toStdString();

  EXPECT_TRUE(t.desktopFile().startsWith("[Desktop Entry]\n"));
  EXPECT_FALSE(t.calls().contains("xdg-mime default "));
}

TEST(NxmHandlerRegistration, OtherHandlerIsKeptUnlessForced)
{
  FakeTools t;
  ScopedEnv bridge("MO2_VFS_BRIDGE", "direct");
  ScopedEnv appImage("APPIMAGE", nullptr);
  ScopedEnv path("PATH", t.pathWithSh().c_str());
  ScopedEnv xdg("XDG_DATA_HOME", t.data.c_str());

  t.setDefault("vortex-nxm.desktop");

  auto [ok, message] = registerAndWait(false);
  ASSERT_TRUE(ok) << message.toStdString();
  EXPECT_TRUE(t.desktopFile().isEmpty());
  EXPECT_FALSE(t.calls().contains("xdg-mime default "));

  std::tie(ok, message) = registerAndWait(true);
  ASSERT_TRUE(ok) << message.toStdString();
  EXPECT_FALSE(t.desktopFile().isEmpty());
  EXPECT_TRUE(t.calls().contains(
      "xdg-mime default modorganizer2-nxm-handler.desktop x-scheme-handler/nxm"));
}

TEST(NxmHandlerRegistration, MissingXdgMimeFails)
{
  TempDir empty, data;
  ScopedEnv bridge("MO2_VFS_BRIDGE", "direct");
  ScopedEnv appImage("APPIMAGE", nullptr);
  ScopedEnv path("PATH", empty.path().c_str());
  ScopedEnv xdg("XDG_DATA_HOME", data.path().c_str());

  const auto [ok, message] = registerAndWait(false);
  EXPECT_FALSE(ok);
  EXPECT_TRUE(message.contains("xdg-mime")) << message.toStdString();
  EXPECT_FALSE(fs::exists(data.path() / "applications"));
}

TEST(NxmHandlerRegistration, ContainerWithoutIdFails)
{
  FakeTools t;
  ScopedEnv bridge("MO2_VFS_BRIDGE", "host");
  ScopedEnv appImage("APPIMAGE", nullptr);
  ScopedEnv container("CONTAINER_ID", nullptr);
  ScopedEnv path("PATH", t.pathWithSh().c_str());
  ScopedEnv xdg("XDG_DATA_HOME", t.data.c_str());

  const auto [ok, message] = registerAndWait(true);
  EXPECT_FALSE(ok);
  EXPECT_TRUE(message.contains("CONTAINER_ID")) << message.toStdString();
  EXPECT_TRUE(t.calls().isEmpty());
}

TEST(NxmHandlerRegistration, HostModeGoesThroughDistroboxHostExec)
{
  FakeTools t;
  t.script("distrobox-host-exec",
           "echo \"host $*\" >> '" + t.log.string() + "'\nexec \"$@\"\n");
  ScopedEnv bridge("MO2_VFS_BRIDGE", "host");
  ScopedEnv appImage("APPIMAGE", nullptr);
  ScopedEnv container("CONTAINER_ID", "test-box");
  ScopedEnv path("PATH", t.pathWithSh().c_str());
  ScopedEnv xdg("XDG_DATA_HOME", t.data.c_str());

  const auto [ok, message] = registerAndWait(false);
  ASSERT_TRUE(ok) << message.toStdString();

  EXPECT_TRUE(t.calls().contains("host xdg-mime query default x-scheme-handler/nxm"));
  EXPECT_TRUE(t.desktopFile().contains("Exec=distrobox enter test-box -- "))
      << t.desktopFile().toStdString();
}
```

- [ ] **Step 2: Rode para ver falhar**

Run: `distrobox enter mo2-run -- bash -c 'cmake --build build-out/modorganizer --target organizer_tests'`
Expected: FAIL de compilação — `registerHandler` não declarado.

- [ ] **Step 3: Implemente**

Em `deps/modorganizer/src/linux/nxmhandler_linux.h`, antes do fim do namespace:

```cpp
// ok is false only on an error, with its cause in message; nothing changed (the
// handler of another program, without force) is a success
using Done = std::function<void(bool ok, const QString& message)>;

// registers Mod Organizer for nxm:// links (decide(), then the desktop file, `xdg-mime
// default` and `update-desktop-database`, whose failure is only logged). Asynchronous:
// `done` is always called later from the event loop of the application, never from
// inside this call.
void registerHandler(bool force, Done done);

// logs the current handler and the Exec line of our desktop file (debug), for
// NexusSettings::dump(); waits up to 5 seconds for xdg-mime
void dump();
```

Em `deps/modorganizer/src/linux/nxmhandler_linux.cpp`, acrescente os includes:

```cpp
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QProcess>
#include <QSaveFile>

#include <uibase/log.h>

#include <memory>
```

e, dentro de `namespace nxmhandler`, antes de `Environment currentEnvironment()`:

```cpp
namespace
{
using Next = std::function<void(bool ok, const QString& out, const QString& error)>;

// starts `c`; `next` gets its trimmed stdout when it exits with 0, or the cause of the
// failure (could not start, exit code and stderr, crash)
void run(const vfsbridge::Command& c, Next next)
{
  auto* p = new QProcess(QCoreApplication::instance());
  const auto shown = (QStringList{c.program} + c.arguments).join(' ');

  QObject::connect(p, &QProcess::errorOccurred, p,
                   [p, next, shown](QProcess::ProcessError e) {
                     // finished() reports the other errors
                     if (e != QProcess::FailedToStart) {
                       return;
                     }
                     next(false, {},
                          QStringLiteral("cannot run %1: %2").arg(shown, p->errorString()));
                     p->deleteLater();
                   });

  QObject::connect(
      p, &QProcess::finished, p,
      [p, next, shown](int code, QProcess::ExitStatus status) {
        const auto out = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
        const auto err = QString::fromUtf8(p->readAllStandardError()).trimmed();

        if (status != QProcess::NormalExit) {
          next(false, {}, QStringLiteral("%1 crashed").arg(shown));
        } else if (code != 0) {
          next(false, {},
               QStringLiteral("%1 exited with %2%3")
                   .arg(shown)
                   .arg(code)
                   .arg(err.isEmpty() ? QString() : ": " + err));
        } else {
          next(true, out, {});
        }

        p->deleteLater();
      });

  p->start(c.program, c.arguments);
}

// `done` from the event loop, never from inside the caller
void finish(const Done& done, bool ok, const QString& message)
{
  QMetaObject::invokeMethod(
      QCoreApplication::instance(),
      [done, ok, message] {
        done(ok, message);
      },
      Qt::QueuedConnection);
}

bool writeDesktopFile(const QString& content, QString& error)
{
  if (!QDir().mkpath(applicationsDir())) {
    error = QStringLiteral("cannot create %1").arg(applicationsDir());
    return false;
  }

  QSaveFile f(desktopFilePath());
  if (!f.open(QIODevice::WriteOnly) || f.write(content.toUtf8()) < 0 || !f.commit()) {
    error = QStringLiteral("cannot write %1: %2").arg(desktopFilePath(), f.errorString());
    return false;
  }

  MOBase::log::debug("wrote {}", desktopFilePath());
  return true;
}

// update-desktop-database (failure only logged), then `done`
void updateDatabase(const Done& done)
{
  vfsbridge::Command c;
  try {
    c = updateDatabaseCommand(applicationsDir());
  } catch (const std::exception& e) {
    MOBase::log::debug("update-desktop-database not run: {}", e.what());
    finish(done, true, {});
    return;
  }

  run(c, [done](bool ok, const QString&, const QString& error) {
    if (!ok) {
      MOBase::log::debug("{}", error);
    }
    done(true, {});
  });
}
}  // namespace
```

e, no fim do namespace:

```cpp
void registerHandler(bool force, Done done)
{
  QString content;
  vfsbridge::Command query;

  try {
    content = desktopFileContent(currentEnvironment());
    query   = queryCommand();
  } catch (const std::exception& e) {
    finish(done, false, QString::fromStdString(e.what()));
    return;
  }

  run(query, [force, content, done](bool ok, const QString& current,
                                    const QString& error) {
    if (!ok) {
      done(false, error);
      return;
    }

    const auto action = decide(current, force);
    if (action == Action::None) {
      MOBase::log::debug("nxm links are handled by {}, not changed", current);
      done(true, {});
      return;
    }

    QString writeError;
    if (!writeDesktopFile(content, writeError)) {
      done(false, writeError);
      return;
    }

    if (action == Action::WriteFile) {
      updateDatabase(done);
      return;
    }

    vfsbridge::Command set;
    try {
      set = setDefaultCommand();
    } catch (const std::exception& e) {
      done(false, QString::fromStdString(e.what()));
      return;
    }

    run(set, [done](bool ok, const QString&, const QString& error) {
      if (!ok) {
        done(false, error);
        return;
      }
      MOBase::log::debug("Mod Organizer is now the handler of nxm links");
      updateDatabase(done);
    });
  });
}

void dump()
{
  MOBase::log::debug("nxm handler:");

  try {
    const auto c = queryCommand();
    QProcess p;
    p.start(c.program, c.arguments);
    if (p.waitForFinished(5000) && p.exitStatus() == QProcess::NormalExit &&
        p.exitCode() == 0) {
      MOBase::log::debug(" . default: {}",
                         QString::fromUtf8(p.readAllStandardOutput()).trimmed());
    } else {
      MOBase::log::debug(" . default: (xdg-mime failed: {})", p.errorString());
    }
  } catch (const std::exception& e) {
    MOBase::log::debug(" . default: ({})", e.what());
  }

  QFile f(desktopFilePath());
  if (!f.open(QIODevice::ReadOnly)) {
    MOBase::log::debug(" . {}: not found", desktopFilePath());
    return;
  }

  for (const auto& line : QString::fromUtf8(f.readAll()).split('\n')) {
    if (line.startsWith("Exec=")) {
      MOBase::log::debug(" . {}: {}", desktopFilePath(), line);
    }
  }
}
```


- [ ] **Step 4: Rode os testes**

Run: `distrobox enter mo2-run -- bash -c 'cmake --build build-out/modorganizer --target organizer_tests && build-out/modorganizer/tests/organizer_tests --gtest_filter="NxmHandler*"'`
Expected: os 12 `NxmHandler.*` e os 6 `NxmHandlerRegistration.*` PASS.

- [ ] **Step 5: Formatação e commit**

Run: `distrobox enter mo2-run -- clang-format -i deps/modorganizer/src/linux/nxmhandler_linux.{h,cpp} deps/modorganizer/tests/test_nxmhandler.cpp`

```bash
git add deps/modorganizer/src/linux/nxmhandler_linux.h deps/modorganizer/src/linux/nxmhandler_linux.cpp \
    deps/modorganizer/tests/test_nxmhandler.cpp
git commit -m "deps/modorganizer: Linux: register the nxm handler with xdg-mime"
```

---

### Task 5: `settings_linux.cpp` — o MO2 usa o handler Linux

**Files:**
- Create: `deps/modorganizer/src/linux/settings_linux.cpp`
- Modify: `deps/modorganizer/src/settings.cpp` (`Settings::registerAsMODLHandler`, `NexusSettings::registerAsNXMHandler`, `NexusSettings::dump` em `#ifdef _WIN32`)
- Modify: `deps/modorganizer/src/linux/README.md`

**Interfaces:**
- Consumes: `nxmhandler::registerHandler(bool, Done)`, `nxmhandler::dump()` (Task 4).
- Produces: as definições Linux de `void Settings::registerAsMODLHandler(bool force, bool includeNxm)`, `void NexusSettings::registerAsNXMHandler(bool force)` e `void NexusSettings::dump() const` (declaradas em `settings.h`, sem mudança).

- [ ] **Step 1: Isole as versões Windows**

Em `deps/modorganizer/src/settings.cpp`, envolva as três definições inteiras (do cabeçalho da função até a `}` final) com `#ifdef _WIN32` … `#endif`, cada uma com o comentário de uma linha:

```cpp
#ifdef _WIN32
// nxmhandler.exe registers the modl:// links, then the nxm:// ones; on Linux, see
// linux/settings_linux.cpp
void Settings::registerAsMODLHandler(bool force, bool includeNxm)
{
  … (corpo atual, sem mudança)
}
#endif
```

```cpp
#ifdef _WIN32
// on Linux, see linux/settings_linux.cpp
void NexusSettings::registerAsNXMHandler(bool force)
{
  … (corpo atual)
}
#endif
```

```cpp
#ifdef _WIN32
// on Linux, see linux/settings_linux.cpp
void NexusSettings::dump() const
{
  … (corpo atual)
}
#endif
```

Confira com `unifdef -D_WIN32 deps/modorganizer/src/settings.cpp | diff - <(git show HEAD:deps/modorganizer/src/settings.cpp)` que a visão Windows só ganhou os comentários (se `unifdef` não existir na distrobox: `sudo dnf -y install unifdef`).

- [ ] **Step 2: Escreva `settings_linux.cpp`**

```cpp
// Linux implementations of the functions of settings.cpp that run nxmhandler.exe on
// Windows: Mod Organizer registers itself for nxm:// links with a desktop file and
// xdg-mime (nxmhandler_linux.h, spec of Phase 4a, section 4).

#include "../settings.h"
#include "nxmhandler_linux.h"

#include <QMessageBox>

#include <log.h>

using namespace MOBase;

void Settings::registerAsMODLHandler(bool force, bool includeNxm)
{
  // there is no modl:// handler on Linux (it only exists in nxmhandler.exe)
  if (includeNxm) {
    nexus().registerAsNXMHandler(force);
  }
}

void NexusSettings::registerAsNXMHandler(bool force)
{
  // force: the button of the Nexus settings, the user waits for an answer; otherwise
  // the registration at startup, which only logs
  nxmhandler::registerHandler(force, [force](bool ok, const QString& message) {
    if (ok) {
      if (force) {
        QMessageBox::information(
            nullptr, QObject::tr("nxm links"),
            QObject::tr("Mod Organizer now handles nxm:// links."));
      }
      return;
    }

    if (force) {
      QMessageBox::critical(
          nullptr, QObject::tr("Failed"),
          QObject::tr("Failed to register Mod Organizer for nxm:// links: %1")
              .arg(message));
    } else {
      log::warn("cannot register Mod Organizer for nxm:// links: {}", message);
    }
  });
}

void NexusSettings::dump() const
{
  nxmhandler::dump();
}
```

(`settings_linux.cpp` entra no alvo `organizer` pelo glob de `mo2_configure_target`; não o acrescente a nenhuma lista.)

Em `deps/modorganizer/src/linux/README.md`, depois do item de `settingsutilities_linux.cpp`, acrescente: `` `settings.cpp` → `settings_linux.cpp` (`registerAsNXMHandler`, `registerAsMODLHandler` and `NexusSettings::dump`: the nxm:// handler of `nxmhandler_linux.cpp` instead of `nxmhandler.exe`) ``.

- [ ] **Step 3: Compile e rode a suíte**

Run: `distrobox enter mo2-run -- ./build.sh --only modorganizer --tests`
Expected: compila; `organizer_tests` verdes; `smoke test passed`. No log do smoke (`main window`), aparece `cannot register Mod Organizer for nxm:// links` (CI, sem `xdg-mime`) ou nada de erro (distrobox, com `xdg-mime`): os dois são aceitos; nenhum diálogo bloqueia.

- [ ] **Step 4: Conferência manual rápida na distrobox**

Run: `distrobox enter mo2-run -- bash -c 'd=$(mktemp -d); mkdir -p $d/{config,data,cache} -m 700 $d/rt; env XDG_CONFIG_HOME=$d/config XDG_DATA_HOME=$d/data XDG_CACHE_HOME=$d/cache XDG_RUNTIME_DIR=$d/rt MO2_VFS_BRIDGE=direct QT_QPA_PLATFORM=offscreen timeout 15 install/bin/ModOrganizer --logs 2>&1 | grep -i nxm; ls $d/data/applications; rm -rf $d'`
Expected: sem instância, o MO2 para na seleção (sem janela principal, sem registro) — saída vazia é aceita. Esta conferência só garante que nada quebra na abertura; o registro real é exercitado no checklist (Task 7).

- [ ] **Step 5: Formatação e commit**

Run: `distrobox enter mo2-run -- clang-format -i deps/modorganizer/src/linux/settings_linux.cpp deps/modorganizer/src/settings.cpp`

```bash
git add deps/modorganizer/src/linux/settings_linux.cpp deps/modorganizer/src/settings.cpp \
    deps/modorganizer/src/linux/README.md
git commit -m "deps/modorganizer: Linux: Mod Organizer registers itself for nxm:// links"
```

---

### Task 6: smoke — um `nxm://` de uma segunda invocação chega ao MO2 aberto

**Files:**
- Modify: `tests/smoke/modorganizer-smoke.sh` (função `main_window`)

**Interfaces:**
- Consumes: o log `download requested: <link>` de `OrganizerCore::downloadRequestedNXM` (`deps/modorganizer/src/organizercore.cpp`) e `received external message '<link>'` de `MOApplication::externalMessage`.

- [ ] **Step 1: Escreva o teste (falha se o repasse não funcionar)**

Em `tests/smoke/modorganizer-smoke.sh`, dentro de `main_window`:

1. Declare as variáveis novas no `local` do início: acrescente `app_env link second rc2 got` à linha `local here home instance game marker out rc=0 i found=0 f`.
2. Troque o `env HOME="$home" … "$APP" --logs -i smoke > "$out" 2>&1 < /dev/null &` por um array reaproveitado:

```bash
    app_env=(HOME="$home"
        XDG_CONFIG_HOME="$home/.config" XDG_DATA_HOME="$home/.local/share"
        XDG_CACHE_HOME="$home/.cache" XDG_STATE_HOME="$home/.local/state"
        XDG_RUNTIME_DIR="$work/runtime" TMPDIR="$work/tmp"
        MO2_VFS_BRIDGE=direct QT_QPA_PLATFORM=offscreen
        QTWEBENGINE_CHROMIUM_FLAGS="--no-sandbox --disable-gpu")
    env "${app_env[@]}" "$APP" --logs -i smoke > "$out" 2>&1 < /dev/null &
    pid=$!
```

3. Logo depois de `if ((found)); then` e do `sleep 2` existente, antes do `kill -TERM`, insira:

```bash
        # spec of Phase 4a, section 4.3: a second invocation with an nxm:// link (what
        # the desktop file of the handler runs) is forwarded to the running application
        # (MOMultiProcess, same TMPDIR) and exits; the running one logs the download
        echo "--- ModOrganizer nxm://... (forwarded to the running application)"
        link='nxm://skyrimspecialedition/mods/1/files/2?key=k&expires=1&user_id=1'
        second="$work/second.out"
        rc2=0
        env "${app_env[@]}" timeout -k 5 20 "$APP" "$link" > "$second" 2>&1 < /dev/null ||
            rc2=$?
        cat "$second"
        ((rc2 == 0)) || fail "the nxm:// invocation exited with $rc2"
        got=0
        for ((i = 0; i < 20; i++)); do
            grep -qF "download requested: $link" "$instance"/logs/mo_interface.log "$out" \
                2>/dev/null && { got=1; break; }
            sleep 0.5
        done
        ((got)) || fail "the running application did not receive the nxm:// link"
        echo "nxm:// link forwarded"
```

4. Atualize o comentário do topo do script (item 3) com uma frase: "While it runs, a second `ModOrganizer nxm://…` must exit with 0 and the running one must log `download requested: nxm://…` (the nxm:// handler, spec of Phase 4a)."

- [ ] **Step 2: Rode o smoke**

Run: `distrobox enter mo2-run -- bash -c 'TMPDIR=$(mktemp -d) tests/smoke/modorganizer-smoke.sh install'`
Expected: `nxm:// link forwarded`, `main window test passed`, `smoke test passed`.

Para ver o teste falhar de propósito (conferência do próprio teste), troque temporariamente `download requested:` por `download requestedX:` no `grep`, rode, confirme `the running application did not receive the nxm:// link`, e desfaça.

- [ ] **Step 3: Commit**

```bash
git add tests/smoke/modorganizer-smoke.sh
git commit -m "tests: smoke: an nxm:// link from a second invocation reaches the running application"
```

---

### Task 7: Documentação, checklist manual e ponto de controle de CI

**Files:**
- Create: `docs/testing/phase4a-manual-checklist.md`
- Modify: `README.md`, `STATE.md`, `ROADMAP.md`

- [ ] **Step 1: Checklist manual**

`docs/testing/phase4a-manual-checklist.md`:

```markdown
# Fase 4a — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-18-phase4a-nexus-nxm-keyring-design.md`](../superpowers/specs/2026-10-18-phase4a-nexus-nxm-keyring-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 compilado na distrobox `mo2-run` (`./build.sh`, resultado em `install/`) e iniciado **pelo box**: `distrobox enter mo2-run -- <repo>/install/bin/ModOrganizer`. Firefox (Flatpak) logado no Nexus. Instância de Skyrim SE das fases anteriores.

**Antes de começar:** no host, `xdg-mime query default x-scheme-handler/nxm` (anote; esperado: vazio) e, no KWallet Manager, nenhuma entrada `Mod Organizer 2: …`.

| # | Passo | Esperado | Resultado |
|---|---|---|---|
| 1 | Configurações → Nexus → conectar (OAuth). Autorizar no navegador. | O navegador do host abre; depois de autorizar, o MO2 mostra a conta. | |
| 1b | Fechar e reabrir o MO2. | Continua conectado, sem pedir login. No KWallet Manager, entrada `Mod Organizer 2: ModOrganizer2_NEXUS_OAUTH_TOKENS`. | |
| 2 | Desconectar do Nexus nas configurações. | A entrada some do KWallet. | |
| 2b | Conectar por API key (colar a chave). Fechar e reabrir. | Continua conectado; entrada `…_APIKEY` no KWallet. | |
| 3 | No host: `xdg-mime query default x-scheme-handler/nxm`; `cat ~/.local/share/applications/modorganizer2-nxm-handler.desktop`. | `modorganizer2-nxm-handler.desktop`; `Exec=distrobox enter mo2-run -- <caminho>/ModOrganizer %u`. | |
| 4 | Com o MO2 aberto: no site do Nexus, "Mod Manager Download" de um mod pequeno de Skyrim SE. | O Firefox pode perguntar com qual programa abrir (aceite e lembre); o download aparece na aba Downloads e termina. | |
| 4b | Fechar o MO2 e repetir com outro mod. | O MO2 abre na instância e baixa. | |
| 5 | Instalar o mod do item 4 e um mod FOMOD baixado do mesmo jeito; ativar. | Instalação normal (FOMOD com o assistente); arquivos na aba Data. | |
| 6 | No host: `xdg-mime default firefox.desktop x-scheme-handler/nxm` (simula outro handler). Reabrir o MO2. Depois, Configurações → Nexus → "Associate with Download Manager Links". | Reabrir não troca (`xdg-mime query …` = `firefox.desktop`); o botão mostra "now handles nxm:// links" e o query volta a `modorganizer2-nxm-handler.desktop`. | |
| 7 | `distrobox enter mo2-run -- env DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent <repo>/install/bin/ModOrganizer`; conectar por API key. | Aviso "Login not saved" uma vez; conectado durante a sessão; ao reabrir normalmente, a chave do item 2b continua no KWallet (nada foi apagado). | |

**Ao terminar:** registre aqui o resultado de cada item e, em "Observações", o que precisou de correção.

## Observações
```

- [ ] **Step 2: README, STATE e ROADMAP**

- `README.md`: na lista de limitações, troque o item "Nexus and Steam logins are not persisted on Linux yet (kept in memory only; a secret-store …)" por "Nexus and Steam logins are kept in the Secret Service (KWallet, GNOME Keyring); without one, only until Mod Organizer closes." e troque "No Python plugins and no `nxm://` link handler until Phase 4." por "No Python plugins until Phase 4d. `nxm://` links: Mod Organizer registers itself as the handler (a desktop file in `~/.local/share/applications`) when no other program handles them; Settings → Nexus forces it." Acrescente `libsecret` à lista de pacotes de execução se ainda não estiver (Task 1).
- `ROADMAP.md`: na tabela "Visão geral", troque a linha da Fase 4 por cinco linhas `4a`–`4e` com o conteúdo da tabela §1.1 da spec da 4a (status: 4a "🟡 Em andamento", as outras "⚪ Não iniciada"); na seção "## Fase 4 — Ecossistema", acrescente no topo `Dividida em subfases (spec da 4a, §1.1).` e a linha `Fase 4a — Spec: … · Plano: … · Checklist manual: …` com os três links.
- `STATE.md`: "**Fase atual:** 4a — Nexus, `nxm://` e keyring · **Status:** 🟡 implementação concluída; falta o CI, o teste manual e o merge"; seção nova "## Fase 4a — progresso" no topo (antes da Fase 3) com a tabela das Tasks 1–8 e os commits principais; em "Limitações conhecidas", seção "Fase 4a" com: carteira trancada faz a interface esperar o prompt; uma instalação trata todos os links; sem `modl://`; MO2 fechado abre a última instância; e risque nas limitações da Fase 1 "Logins … só em memória" e o `nxm://` de "Sem LOOT, …".

- [ ] **Step 3: Commit**

```bash
git add docs/testing/phase4a-manual-checklist.md README.md STATE.md ROADMAP.md
git commit -m "docs: Phase 4a checklist, state and roadmap"
```

- [ ] **Step 4: Ponto de controle de CI (com o usuário)**

Peça autorização ao usuário para enviar o branch. Com ela:

```bash
git commit --allow-empty -m "ci: Phase 4a checkpoint [windows-ci]"
git push -u origin fase-4a-nexus
```

Acompanhe com `gh run list --branch fase-4a-nexus` e `gh run watch <id>`.
Expected: Linux verde (com `secret_service_integration` **Passed** — confira no log do passo de testes que não foi `Skipped`) e Windows verde. Uma falha vira tarefa de correção antes da Task 8 (systematic-debugging); registre no STATE.

---

### Task 8: Teste manual com o usuário e fechamento

**Files:**
- Modify: `docs/testing/phase4a-manual-checklist.md`, `STATE.md`, `ROADMAP.md`, `docs/superpowers/specs/2026-10-18-phase4a-nexus-nxm-keyring-design.md` (status)

- [ ] **Step 1: Pacote e roteiro**

Compile o branch na distrobox (`distrobox enter mo2-run -- ./build.sh`) e entregue ao usuário o checklist (`docs/testing/phase4a-manual-checklist.md`) com os comandos exatos do seu ambiente (caminho de `install/bin/ModOrganizer`).

- [ ] **Step 2: Achados**

Cada item que falhar vira uma correção com o fluxo de systematic-debugging (causa raiz antes de corrigir), teste automático quando possível, commit próprio (`deps/modorganizer: Linux: …` ou `docs: …`) e uma linha em "Observações" do checklist e nas "Decisões tomadas" do `STATE.md` (data 2026-10-…, o que mudou e por quê). Riscos já previstos na spec §6: redirect do OAuth para `localhost`, "abrir com…" do Firefox Flatpak, cache `sycoca` do Plasma (rodar `kbuildsycoca6` no host se o `.desktop` não for visto), nomes de arquivo do `DownloadManager`.

- [ ] **Step 3: Fechamento**

Com o checklist aprovado pelo usuário e o CI verde no último commit:
- spec: `**Status:** aprovada; implementada (teste manual aprovado em <data>)`;
- `ROADMAP.md`: 4a "✅ Concluída (<data>)";
- `STATE.md`: Fase 4a concluída, próxima 4b (LOOT);
- commit `docs: Phase 4a manual test passed [windows-ci]`, push, e — com autorização do usuário — merge em `main` (superpowers:finishing-a-development-branch).
