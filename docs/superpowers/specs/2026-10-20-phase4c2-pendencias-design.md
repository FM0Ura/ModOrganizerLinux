# Fase 4c.2 — Pendências da Fase 2 e INIs — Design

- **Data:** 2026-10-20
- **Status:** em revisão
- **Spec geral:** [`2026-10-14-mo2-linux-port-design.md`](2026-10-14-mo2-linux-port-design.md) (§3.3 resume a Fase 4; este documento a substitui onde divergirem)
- **Divisão da 4c:** [`2026-10-20-phase4c1-cpp-plugins-design.md`](2026-10-20-phase4c1-cpp-plugins-design.md) §1.2; esta é a 4c.2 (pendências adiadas), entre a 4c.1 (plugins C++) e a 4c.3 (ferramentas pelo VFS)
- **Branch:** `fase-4c2-pendencias` (a partir de `fase-4c1-plugins`, no worktree `../ModOrganizerLinux-4c2`; o PR mira `fase-4c1-plugins` até a 4c.1 entrar no `main`)

## 1. Objetivo

Fechar as pendências adiadas da Fase 2 (STATE.md, "Pendências por fase → Fase 2", itens 1–5 e 7; o 6 foi resolvido na Fase 3), os dois follow-ups de INI da revisão final da 4c.1 e duas sobras vizinhas que tocam o mesmo código. Meta: sem tela o MO2 diz o motivo e sai; um helper travado nunca deixa o refresh varrer um mount vivo, e o refresh adiado volta sozinho quando o mount some; as listas de arquivos (BSA/BA2) e a invalidação de BSA leem e gravam o INI certo mesmo com maiúsculas diferentes; o checklist da Fase 2 descreve o que de fato acontece.

### 1.1 Itens desta fase

| Id | Origem | Item |
|---|---|---|
| P1 | Fase 2, teste manual, item 1 | Sem `WAYLAND_DISPLAY`/`DISPLAY` (ex.: SSH), o MO2 aborta com "has crashed" e backtrace; deve dizer que não há tela e sair. |
| P2 | Fase 2, item 2 | Konsole como programa de teste precisa de `--separate --nofork` (checklist e exemplo de executável). |
| P3 | Fase 2, item 3 | Checklist: rodar o MO2 por SSH exige `WAYLAND_DISPLAY`, `XDG_SESSION_TYPE`, `XDG_RUNTIME_DIR`, `DISPLAY` e `XAUTHORITY`; ou rodar do Konsole da área de trabalho. |
| P4 | Fase 2, item 4 | Checklist item 6b: pela distrobox com `\| tee`, Ctrl+C/fechar o terminal derruba só o cliente do podman; o MO2 continua aberto. |
| P5 | Fase 2, item 5 | Checklist item 8: o MO2 cria `logs/crash.log` vazio ao iniciar; checar "não vazio". |
| P7 | Fase 2, revisão final | Helper travado com grupo observado: `helperLost()` chama `deactivated()` com a raiz ainda montada, e o refresh adiado pode varrer um mount vivo ou travado. |
| R1 | Fase 3, sobras | O estado "não foi possível desmontar" nunca reconfere a tabela de mounts; o refresh fica adiado até o próximo "Executar". |
| R2 | Fase 4, pendências | `MO2_VFS_TIMEOUT_MS` também pode alongar o timeout; limitar a 30 s. |
| I1 | 4c.1, revisão final | A invalidação de BSA do Fallout 4 usa `Fallout4Custom.ini`; o perfil tem `fallout4custom.ini` (`iniFiles()`), e no Linux é criado um segundo arquivo. |
| I2 | 4c.1, revisão final | Os `*DataArchives` leem e gravam `skyrim.ini`, `fallout4.ini` etc. em minúsculas; no prefixo os arquivos são `Skyrim.ini`, `Fallout4.ini`. |

### 1.2 Decisões desta fase

| Tema | Decisão |
|---|---|
| Escopo (decisão do usuário, 2026-10-20) | Pendências da Fase 2 + follow-ups de INI da 4c.1 + R1 e R2 (vizinhas do P7). As demais pendências da Fase 4 do STATE.md ficam fora. |
| Reconferência da tabela de mounts (R1) | Por timer, assíncrona (abordagem A): enquanto há raízes deixadas montadas, a cada 5 s roda `mo2-vfs --cleanup` e depois `mo2-vfs --list-mounts` por `QProcess` assíncrono, sem travar a interface. Descartadas: reconferir só em momentos fixos (antes do "Executar", no Unlock, ao focar a janela), que mantém o refresh dependente de ação do usuário; inotify em `/proc/self/mountinfo`, que não vê o mount do host com o MO2 no box. |
| Nada desmonta um mount de helper vivo | O `--cleanup` só desmonta raízes de helpers mortos (arquivo de estado com pid inexistente, `state::cleanup_stale_in`). Um helper travado, mas vivo, pode ter um jogo usando o mount; um desmonte preguiçoso tiraria os arquivos dos mods de baixo do jogo. |
| Sem tela (P1) | Checagem antes de criar a `QApplication`, só para as plataformas `wayland*`/`xcb*` ou sem plataforma definida; `DISPLAY` definido mas inalcançável continua no `qFatal` da Qt (limitação). |
| INIs ignorando maiúsculas (I1, I2) | No ponto central de leitura e escrita dos INIs de arquivos (`GamebryoDataArchives`, `MorrowindDataArchives`) e na invalidação de BSA (`GamebryoBSAInvalidation`), pelo `GameGamebryo::resolveIgnoringCase()` da 4c.1 (identidade no Windows); sem tocar a lista de nomes de cada jogo. |
| Patches | `deps/modorganizer: Linux: …` e `deps/game_bethesda: Linux: …`, como nas fases anteriores; código Windows idêntico ou em `#ifdef _WIN32`. |

## 2. VFS: helper travado e reconferência (`deps/modorganizer/src/linux/vfsconnector_linux.cpp`)

### 2.1 P7: helper travado com programa observado

Em `helperLost()`, o ramo "helper ainda vivo e `m_watched > 0`" deixa de chamar `deactivated()`. Passa a chamar `keepLeftMounted(m_roots, motivo)` e `m_cleanupNeeded = true`: a raiz conta como ainda montada (`isActive()` verdadeiro), o callback de desativação não dispara e o refresh adiado não roda sobre o mount travado. O motivo entra em `m_lostReason` (o aviso de VFS perdido do `afterRun` continua mostrando-o). O helper não é morto (um programa pode estar usando o mount), como hoje.

### 2.2 R1: reconferência por timer

- `keepLeftMounted()` liga um `QTimer` de reconferência (5 s; `MO2_VFS_RECHECK_MS` encurta o intervalo nos testes, nunca alonga); `deactivated()` o desliga.
- A cada disparo, se não houver reconferência em andamento: roda `mo2-vfs --cleanup` e, ao terminar, `mo2-vfs --list-mounts`, ambos por `vfsbridge::helperCommand()` (pelo `distrobox-host-exec` quando o MO2 está no box) em `QProcess` assíncrono, com prazo de 10 s cada (passou do prazo: o processo é morto e a tabela conta como ilegível).
- O parse da saída do `--list-mounts` sai de `mountedRoots()` para uma função comum (`parseMountedRoots()`), usada pelo caminho síncrono atual e pelo assíncrono.
- Resultado: se nenhuma das raízes de `m_leftMounted` está montada, o helper perdido que ainda estiver vivo é descartado (§2.3) e `deactivated()` roda (o refresh adiado acontece). Se alguma continua montada, nada muda e o próximo disparo tenta de novo. Tabela ilegível: as raízes continuam contando como montadas.
- O evento `unmounted` de um helper que se recupera continua chegando pelo caminho atual (`setUnmountedHandler` → `deactivated()`), que limpa o estado e desliga o timer.
- O conector é destruído com o timer e os processos de reconferência (filhos do contexto do conector); um processo em andamento é morto no destrutor.

### 2.3 Helper perdido ainda vivo

Um helper perdido (o `VfsClient` não está mais utilizável depois de um timeout) que ainda roda é morto e descartado (`dropLostHelper()`) só quando o mount dele já não existe:
- na reconferência do §2.2, quando as raízes não estão mais montadas;
- em `doActivate()`: o helper perdido e ainda vivo deixa de ser descartado logo no início (hoje o destrutor do `VfsClient` fecha a entrada, espera até 5 s e o deixa rodando); ele fica guardado até `cleanupLeftMounts()` confirmar que não há raízes montadas, e então é morto antes do `start()`. Com raízes ainda montadas, o erro "still mounted" de hoje continua, e a reconferência o mata depois.
- `spawn()` sem VFS não muda: ele precisa de um helper na hora e continua descartando o perdido como hoje (sem matá-lo, porque o mount dele pode estar em uso).

Matar o helper é seguro nesses pontos porque seu mount já não existe; o `SIGKILL` vai só para o pid do helper, e os programas que ele iniciou estão em grupos de processos próprios (`process_group(0)` no `spawn` do `mo2-vfs`), então não são afetados.

### 2.4 R2: limite do timeout

`mountTimeout()` vira `vfsMountTimeout()` = `min(MO2_VFS_TIMEOUT_MS, 30 s)`, ao lado de `vfsRecheckInterval()` (§2.2), num arquivo pequeno testável (`linux/vfstimeouts_linux.{h,cpp}`, biblioteca `organizer_launch`): as variáveis (de testes) só encurtam os valores; vazias, zero, negativas ou inválidas valem o padrão.

## 3. Sem tela (P1)

- Função pura `missingDisplayMessage(args, env)` em `deps/modorganizer/src/linux/displaycheck_linux.{h,cpp}` (biblioteca `organizer_desktop`, só no Linux): a plataforma é a de `-platform <x>` ou `--platform <x>` nos argumentos (as formas que a Qt aceita), senão `QT_QPA_PLATFORM`. Se a plataforma é vazia ou começa por `wayland` ou `xcb` (também listas como `wayland;xcb`), e `WAYLAND_DISPLAY` e `DISPLAY` estão vazios, devolve a mensagem; senão, nada. `offscreen`, `minimal`, `vnc` e outras sempre passam.
- Em `run()` (`main.cpp`, `#ifndef _WIN32`): depois de `cl.process()` (que trata `--help` e os comandos sem interface) e de `initLogging()`, antes de `MOApplication`. Com mensagem: grava no log (`log::error`) e na saída de erro, e `return 1`. Sem backtrace, sem `crash.log` preenchido.
- Mensagem (inglês, como as demais do código-fonte): *"Mod Organizer needs a graphical session, but neither WAYLAND_DISPLAY nor DISPLAY is set. Run it from the desktop, or set WAYLAND_DISPLAY, XDG_RUNTIME_DIR, DISPLAY and XAUTHORITY to those of the desktop session."*

## 4. INIs ignorando maiúsculas (I1, I2) (`deps/game_bethesda`)

- `GamebryoDataArchives::getArchivesFromKey()` e `setArchivesToKey()`: o caminho do INI passa por `GameGamebryo::resolveIgnoringCase()` antes do `GetPrivateProfileStringW`/`WriteRegistryValue`. Corrige de uma vez os `*DataArchives` que os usam (Skyrim, Skyrim SE/VR, Enderal/SE, Fallout 3/NV/4/4 VR/London/76, TTW, Oblivion, Nehrim, Starfield), com INIs do perfil ou do prefixo.
- `MorrowindDataArchives::getArchives()` e `setArchives()`: idem.
- `GamebryoBSAInvalidation::prepareProfile()`: `iniFilePath` resolvido do mesmo jeito; corrige o `Fallout4Custom.ini` × `fallout4custom.ini` e os equivalentes dos outros jogos.
- Sem variante existente, o nome fica como veio (comportamento atual de `resolveIgnoringCase()`), e o arquivo é criado com ele. No Windows, `resolveIgnoringCase()` devolve o caminho dado.
- O plano confere, no código, que todos os `*DataArchives` e `*BSAInvalidation` passam por esses três pontos; um que leia o INI por outro caminho recebe a mesma chamada.

## 5. Documentação (P2–P5)

- `docs/testing/phase2-manual-checklist.md`:
  - P2: exemplo do Konsole com `--separate --nofork -e …`, com o motivo (sem `--separate` ele entrega a janela ao Konsole já aberto pelo D-Bus e sai na hora; o VFS desmonta).
  - P3: quadro "rodar por SSH" com as cinco variáveis (`WAYLAND_DISPLAY`, `XDG_SESSION_TYPE`, `XDG_RUNTIME_DIR`, `DISPLAY`, `XAUTHORITY`; sem `XAUTHORITY` programas X11 como o `xterm` falham com "Authorization required"), ou rodar do Konsole da área de trabalho.
  - P4: item 6b: com `| tee`, Ctrl+C ou fechar o terminal encerra só o cliente do podman; o MO2 (e o mount) continuam; fechar pelo MO2.
  - P5: item 8: "nenhum `crash.log` novo **não vazio**" (o MO2 cria um vazio ao iniciar); idem em "O que enviar se falhar".
- README, seção Bazzite: uma linha sobre rodar por SSH (as variáveis do P3) junto da nota de `QT_QPA_PLATFORM`.
- STATE.md: fase atual, itens marcados como resolvidos em "Pendências por fase", limitações da 4c.2, decisões.

## 6. Testes

| Item | Teste |
|---|---|
| P7 | `test_vfsconnector.cpp`, com os helpers falsos existentes: helper que para de responder a um `update` com programa observado → `isActive()` continua verdadeiro e o callback não dispara; a raiz aparece em `m_leftMounted`/`staleMounts()`. |
| R1 | Helper falso cujo `--list-mounts` lê a lista de um arquivo do teste: com a raiz listada, nada dispara; ao esvaziar a lista, o callback dispara em poucos intervalos (`MO2_VFS_RECHECK_MS` curto); `--list-mounts` com falha mantém o estado. |
| §2.3 | Depois da reconferência, o helper travado foi morto (pid ausente) e o `activate()` seguinte monta com um helper novo; com a raiz ainda listada, `activate()` falha com "still mounted" e o helper travado continua vivo; com a raiz já fora da tabela (antes da reconferência), `activate()` mata o helper travado e monta com um novo. |
| R2 | `vfsMountTimeout()`/`vfsRecheckInterval()`: sem variável → 30 s/5 s; `200`/`100` → 200 ms/100 ms; `999999` → 30 s/5 s; `0`, `-5`, `abc` → padrão. |
| P1 | gtest de `missingDisplayMessage()`: sem variáveis → mensagem; `WAYLAND_DISPLAY` ou `DISPLAY` → nada; `QT_QPA_PLATFORM=offscreen` → nada; `-platform offscreen`/`--platform offscreen` vencem a variável; `wayland;xcb` → mensagem. Smoke test (`tests/smoke/modorganizer-smoke.sh`): `env -u WAYLAND_DISPLAY -u DISPLAY -u QT_QPA_PLATFORM` → código 1, mensagem na saída de erro, sem "has crashed" nem `crash.log` não vazio. |
| I1, I2 | `deps/game_bethesda/tests/test_profile_inis_linux.cpp`: arquivos lidos e gravados no `Skyrim.ini` existente quando o código pede `skyrim.ini` (perfil com INIs locais e prefixo sem INIs locais); invalidação de BSA do Fallout 4 grava no `fallout4custom.ini` existente sem criar `Fallout4Custom.ini`; sem nenhum arquivo, cria com o nome pedido. |
| Regressão | Toda a suíte (`--tests`), smoke, `plugin-load`, `vfs-e2e`, `loot-e2e` no CI Linux; CI Windows pelo `[windows-ci]`. |

Checklist manual `docs/testing/phase4c2-manual-checklist.md` (curto):
1. Sem tela: por SSH sem as variáveis, o MO2 mostra a mensagem e sai com código 1, sem "has crashed".
2. INIs: com o Skyrim SE (e o Fallout 4, se instalado), ativar/desativar a invalidação de arquivos e conferir que `SResourceArchiveList`/`bInvalidateOlderFiles` mudam no INI real (`Skyrim.ini`/`Fallout4Custom.ini` do prefixo ou o do perfil) e que nenhum INI duplicado com outra caixa aparece.
3. Helper travado: com um jogo/programa rodando pelo MO2, `kill -STOP` no `mo2-vfs` e um refresh (F5) → o refresh fica adiado (sem lista vazia); `kill -CONT`, fechar o programa → o refresh adiado roda em até ~5 s depois de o mount sumir.
4. Regressão rápida: Skyrim SE + SKSE com mods pelo "Executar".

## 7. Riscos

| Risco | Mitigação |
|---|---|
| A reconferência roda um processo a cada 5 s pela ponte (`distrobox-host-exec`, ~100 ms+) | Só enquanto há raízes deixadas montadas (estado raro); assíncrona; uma por vez. |
| `--cleanup` desmontar um mount em uso | Só raízes de pid morto (código do `mo2-vfs`, sem mudança); teste com helper vivo e raiz listada. |
| Matar o helper perdido encerrar programas do usuário | Só depois de confirmado que o mount não existe; o sinal vai só ao pid do helper, e os programas estão em grupos de processos próprios. |
| `resolveIgnoringCase()` achar outro arquivo com o mesmo nome em outra caixa (dois arquivos) | Já prefere o nome exato (teste da 4c.1 `PrefersTheExactName`). |
| Testes de timer instáveis no CI | Intervalo curto por variável, espera com prazo folgado pelo `QEventLoop`, como os testes atuais do conector. |

## 8. Critérios de pronto

- P1, P7, R1, R2, I1, I2 implementados com os testes do §6 passando localmente (`--tests`, com `TMPDIR` curto próprio no worktree).
- P2–P5, README e STATE.md atualizados.
- CI Linux e Windows verdes no ponto de controle `[windows-ci]`.
- Teste manual do usuário com `docs/testing/phase4c2-manual-checklist.md`; só então a spec passa a "aprovada; implementada" e o PR entra (depois da 4c.1).
