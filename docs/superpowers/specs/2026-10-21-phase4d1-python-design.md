# Fase 4d.1 — Plugins Python (runtime e plugins pequenos) — Design

- **Data:** 2026-10-21
- **Status:** em revisão
- **Spec geral:** [`2026-10-14-mo2-linux-port-design.md`](2026-10-14-mo2-linux-port-design.md) (§3.3 resume a Fase 4; este documento a substitui onde divergirem)
- **Divisão da 4d (decisão do usuário, 2026-10-21):** 4d.1 é o `plugin_python` (Python embutido + `mobase` + PyQt6) e os plugins Python pequenos que acompanham o MO2; 4d.2 é o `basic_games`, com a detecção pela Steam e pelo Proton. Um PR cada, empilhados como na 4c.
- **Branch:** `fase-4d1-python` (a partir de `fase-4c3-ferramentas`, no worktree `../ModOrganizerLinux-4d1`; o PR mira `fase-4c3-ferramentas` até as anteriores entrarem no `main`)

## 1. Objetivo

Carregar plugins Python no MO2 do Linux, como no Windows: o `plugin_python` inicia um Python embutido, expõe a API do MO2 (`mobase`, por pybind11) e os plugins usam PyQt6. Meta: os seis plugins Python que acompanham o MO2 2.5 (fora o `basic_games`) carregam e funcionam no Skyrim SE do usuário; o Python do MO2 não enxerga pacotes instalados pelo usuário.

### 1.1 Itens desta fase

| Item | Repositório do upstream | O que é |
|---|---|---|
| `plugin_python` | `modorganizer-plugin_python` | proxy (plugin C++ carregado pelo MO2), runner (inicia o Python), `mobase` (módulo de extensão), `pybind11-qt` e `pybind11-utils` |
| `preview_dds` | `modorganizer-preview_dds` | pré-visualização de `.dds` (OpenGL pelo PyQt6) |
| `form43_checker` | `modorganizer-form43_checker` | diagnóstico: plugins no formato 43 no Skyrim SE |
| `script_extender_plugin_checker` | `modorganizer-script_extender_plugin_checker` | diagnóstico: DLLs do script extender que não carregaram |
| `tool_configurator` | `modorganizer-tool_configurator` | editor dos INIs do jogo (pyCfg) |
| `installer_wizard` | `modorganizer-installer_wizard` | instala arquivos BAIN com script `wizard.txt` |
| `fnistool` | `modorganizer-fnistool` | roda o FNIS pelo MO2 (saída num mod) |

### 1.2 Decisões desta fase

| Tema | Decisão |
|---|---|
| Python e PyQt6 (abordagem A, decisão do usuário) | Python 3.14, pybind11 e PyQt6 do Fedora, na imagem do container (`build/Containerfile`) e no box `mo2-run`. O PyQt6 do Fedora é compilado contra o mesmo Qt 6 do sistema que o MO2 usa: um só Qt no processo. Descartadas: Python 3.13 compilado à parte (mais build, PyQt6 teria de ser compilado contra o nosso Qt); PyQt6 do `pip` (traz um Qt próprio: dois Qt no processo). |
| Isolamento (decisão do usuário) | Configuração isolada do Python (sem site do usuário, sem `PYTHONPATH`, `PYTHONHOME` e demais variáveis `PYTHON*`) **e** `sys.path` filtrado ao iniciar: só a biblioteca padrão do Python do container, o `site-packages` dos RPMs (`/usr/lib64/python3.14/site-packages` e `/usr/lib/python3.14/site-packages`) e as pastas do MO2. Nada de `~/.local` (o `$HOME` da distrobox é o do host), nem de `/usr/local`. Sem venv: ele usaria a mesma biblioteca padrão e o PyQt6 do sistema, com mais peças. |
| Versão do Python no CMake | No Linux, `MO2_PYTHON_VERSION` é a versão do Python do sistema (3.14), em vez dos 3.13 exatos do upstream; o Windows não muda. |
| Dependências próprias de plugins | Instaladas pelo `pip install --target` do `cmake_common` (`mo2_python_pip_install`) na pasta do plugin, como no Windows (o `installer_wizard` precisa de `bain-wizard-interpreter==1.0.4`); o build precisa de rede para isso. |
| Importação | `git subtree add --squash` do `master` de cada repositório em `deps/<nome>`, patches no superprojeto (`deps/<nome>: Linux: …`), como na 4c.1. Código Windows idêntico ou em `#ifdef _WIN32` (C++) / `if sys.platform == "win32"` (Python). |
| Empacotamento | O MO2 com plugins Python roda onde o Python 3.14 e o PyQt6 do Fedora existem: o box `mo2-run` (como o usuário já roda). Python e PyQt6 dentro do pacote ficam para o AppImage da Fase 5. |

## 2. Dependências e CMake

- **Imagem do container e box:** `pybind11-devel`, `python3-pyqt6`, `python3-pyqt6-devel` (traz `pyuic6` e `pylupdate6`), `python3-pip` e `python3-pytest`. O `python3-devel` já está lá. O README diz como instalar no box `mo2-run` existente (`sudo dnf install …`).
- **`cmake_common` (`mo2_versions.cmake`, `mo2_python.cmake`, `mo2_utils.cmake`):** no Linux, `MO2_PYTHON_VERSION` vem do Python do sistema; `mo2_python_install_pyqt()` não copia o PyQt6 (cria o alvo apontando para o do sistema); `pyuic6` e `pylupdate6` vêm do `PATH` (no Windows continuam `…/bin/pyuic6.exe` e `pylupdate6.exe`).
- **`build.sh`:** os novos subtrees entram na ordem de build depois do `modorganizer` (o `plugin_python` depende do `uibase`; os plugins Python, do `plugin_python` instalado para os stubs e a tradução).

## 3. `plugin_python` no Linux

### 3.1 Proxy (`src/proxy/proxypython.cpp`)

- **Pasta do plugin:** por `dladdr()` do endereço de uma função do proxy, em vez de `GetModuleHandleEx`/`GetModuleFileName`.
- **`libpython` global:** antes de criar o runner, o proxy abre o `libpython3.14.so` do sistema com `dlopen(RTLD_NOW | RTLD_GLOBAL)` (nome da biblioteca vindo do CMake, `Python_LIBRARY`). Os módulos de extensão do Fedora (PyQt6, `mobase`) não se ligam ao `libpython` e precisam dos símbolos dele no escopo global; o MO2 carrega plugins com `RTLD_LOCAL`. Substitui o `SetDllDirectoryW` do Windows.
- **Runner:** biblioteca compartilhada em `plugins/plugin_python/dlls/` (mesma disposição do Windows), ligada ao proxy com `RUNPATH` `$ORIGIN/plugin_python/dlls`.
- **Mensagens de falha:** o tipo `DLL_NOT_FOUND` passa a dizer, no Linux, que o Python 3.14 ou o PyQt6 não foram encontrados no sistema e quais pacotes instalar no box; `SEMICOLON` (caminho com `;`) só existe no Windows.

### 3.2 Runner (`src/runner/pythonrunner.cpp`)

- Continua com `PyConfig_InitIsolatedConfig` (sem site do usuário e sem variáveis `PYTHON*`).
- No Linux, `module_search_paths` não é substituído pelas pastas do MO2 (não há `pythoncore.zip`): o Python usa a biblioteca padrão do sistema. Depois de iniciar, o runner **filtra** o `sys.path`: mantém só as entradas sob o prefixo do Python do sistema (`sys.base_prefix`, a biblioteca padrão e `lib-dynload`) e os dois `site-packages` dos RPMs, e acrescenta as pastas do MO2 (a pasta `libs` do plugin e a pasta de dados dos plugins, `IOrganizer::getPluginDataPath()`). Entradas de `/usr/local`, de `~/.local` ou de qualquer outro lugar saem.
- O `argv[0]` (`"ModOrganizer.exe"`) não muda: só identifica o processo para o Python.

### 3.3 `mobase`

- `HANDLE` e `INVALID_HANDLE_VALUE` vêm do `win32compat` do `uibase` (Fase 1).
- `KnownFolder` (`known_folders.h`): no Linux, a lista tem só as pastas que o `getKnownFolder()` do `uibase` Linux suporta (as de `win32compat.h`); as demais não existem no módulo (um plugin que as use recebe `AttributeError`, como receberia de uma versão antiga).
- O resto dos bindings não depende de Windows.

### 3.4 Instalação

`plugins/plugin_python.so` (proxy); `plugins/plugin_python/dlls/` (runner); `plugins/plugin_python/libs/` (`mobase.cpython-314-x86_64-linux-gnu.so` e os stubs `mobase`). O PyQt6 **não** é copiado. Os plugins Python ficam como no Windows: `plugins/<plugin>/` (pacote) ou `plugins/<plugin>.py`.

## 4. Os plugins Python

Quase todos são Python puro, sem `winreg` nem `os.startfile`. Cada um é revisado por: caminhos montados com `\`, `.exe` executado diretamente, nomes de arquivo com maiúsculas diferentes das do disco, e chamadas a APIs do Windows. Correções em `if sys.platform == "win32"`.

- **`fnistool`:** roda `GenerateFNISforUsers.exe` pelo `organizer.startApplication()`, que no Linux vai por Proton com o VFS (Fases 3 e 4c.3); a revisão confere o caminho da ferramenta e o argumento `RedirectFiles` (caminho de mod em formato Windows: `Z:\…`).
- **`preview_dds`:** `QOpenGLWidget`/`QOpenGLTexture` funcionam no Wayland. O `preview_base` sempre pula `.dds` (por desenho do upstream, para o plugin de DDS tratar): com o `preview_dds` instalado, `.dds` passa a ter pré-visualização (a pendência registrada no STATE.md da 4c.1).
- **`installer_wizard`:** `bain-wizard-interpreter` instalado na pasta do plugin (§1.2).

## 5. Testes

- **`plugin_python` (upstream):** os testes do runner (gtest, `tests/runner`) e os testes Python do `mobase` (pytest, `tests/python`, com os módulos C++ de apoio), rodando no `build.sh --tests`.
- **Isolamento (novo):** (a) o filtro do `sys.path` é uma função separada, testada com listas de caminhos (entradas da biblioteca padrão, dos dois `site-packages` dos RPMs, de `/usr/local/lib/python3.14/site-packages`, de `~/.local/...` e do MO2: só a primeira, a segunda e a última família ficam, na ordem original, com as pastas do MO2 no fim); (b) integração no runner: com um HOME temporário que tem um pacote falso em `~/.local/lib/python3.14/site-packages` e outro pacote falso num diretório de `PYTHONPATH`, o runner inicializado não importa nenhum dos dois e o `sys.path` só tem entradas permitidas.
- **Verificação de carga (`build.sh`):** o proxy carrega como plugin.
- **Smoke:** com o MO2 headless, o log mostra o Python iniciado e os seis plugins Python carregados.
- **Manual (Skyrim SE do usuário):** pré-visualização de um `.dds` do `Mod2_Textures`; os dois verificadores no painel de notificações; o Configurator abrindo os INIs do perfil; um arquivo BAIN com `wizard.txt` (fixture criada nesta fase); opcional: o FNIS.

## 6. Riscos

| Risco | Mitigação |
|---|---|
| O upstream é testado no Python 3.13; algo do pybind11 ou da API do Python muda no 3.14 | Os testes do upstream rodam no Linux; correções pequenas como patches. |
| Símbolos do `libpython` não visíveis aos módulos de extensão | `dlopen(RTLD_GLOBAL)` no proxy (§3.1), coberto pelo smoke (o PyQt6 importa). |
| PyQt6 do Fedora com outra versão do Qt que o MO2 | Ambos vêm do mesmo Fedora 44, na mesma imagem; o smoke importa o PyQt6 dentro do MO2. |
| Box antigo sem os pacotes novos | O proxy diz quais pacotes faltam (§3.1); o README tem o comando. |
| `pip` sem rede no build | Só o `installer_wizard` precisa; o CI tem rede; falha de build clara. |

## 7. Critérios de pronto

- O `plugin_python` inicia o Python 3.14 do sistema com o `sys.path` filtrado; o teste de isolamento passa.
- Os testes do upstream do `plugin_python` passam no Linux.
- Os seis plugins Python carregam (smoke) e funcionam no teste manual.
- O Windows continua compilando (CI `[windows-ci]`).
- README (pacotes do box, plugins Python), checklist manual, ROADMAP e STATE atualizados.
