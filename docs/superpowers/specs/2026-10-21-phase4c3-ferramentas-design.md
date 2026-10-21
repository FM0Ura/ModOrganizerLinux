# Fase 4c.3 — Ferramentas pelo VFS — Design

- **Data:** 2026-10-21
- **Status:** em revisão
- **Spec geral:** [`2026-10-14-mo2-linux-port-design.md`](2026-10-14-mo2-linux-port-design.md) (§1.1, critério 6; §3.3 resume a Fase 4; este documento a substitui onde divergirem)
- **Divisão da 4c:** [`2026-10-20-phase4c1-cpp-plugins-design.md`](2026-10-20-phase4c1-cpp-plugins-design.md) §1.2; esta é a 4c.3 (ferramentas pelo VFS), depois da 4c.2 (pendências da Fase 2)
- **Branch:** `fase-4c3-ferramentas` (a partir de `fase-4c2-pendencias`, no worktree `../ModOrganizerLinux-4c3`; o PR mira `fase-4c2-pendencias` até a 4c.2 entrar no `main`)

## 1. Objetivo

Usar ferramentas Windows (xEdit, BodySlide, Nemesis/Pandora) pelo MO2, enxergando a mesma visão virtual (critério de sucesso 6). A Fase 3 validou só o SSEEdit; esta fase cobre as demais, a partir do que um spike com as ferramentas reais mostrou (§1.1). Meta: uma ferramenta instalada como mod e configurada pelo caminho do `.exe` dentro do mod roda como no Windows, de dentro do `Data` virtual; o xEdit dos outros jogos Bethesda da Steam encontra INIs e `plugins.txt` no prefixo do seu jogo.

### 1.1 Spike (2026-10-21)

Feito no Bazzite com a compilação da 4c.2, numa instância portátil de teste (cópia da instância Skyrim SE do usuário), MO2 no box `mo2-run` e VFS no host, ferramentas baixadas pelo usuário do Nexus:

| Ferramenta | Pelo caminho do mod | Pelo `Data` virtual | Observação |
|---|---|---|---|
| Nemesis 0.84 | ❌ ERROR(1008) "Unable to locate Skyrim Data folder. This tool must be executed in skyrim's data folder" | ✅ Update Engine e Launch | O Nemesis acha o `Data` pela pasta do próprio `.exe` (`CriticalLog.txt`: "Detected Path: X:\…\mods\…\Nemesis_Engine"). Pelo `Data` ele roda como `S:\steamapps\common\Skyrim Special Edition\Data\Nemesis_Engine\…`; gera `.hkx`, `animationdatasinglefile.txt`, `FNIS.pex` (o `PapyrusCompiler.exe` filho funciona) e roda o Python embutido; tudo vai para `overwrite/`. |
| BodySlide 6.0.0 | ✅ | ✅ | Acha o `Data` pelo registro do prefixo (`GameDataPath` = `S:\steamapps\common\Skyrim Special Edition\Data\`); OpenGL e pré-visualização funcionam. |
| Pandora 5.0.0-beta | ✅ | ✅ | Com o build do Nexus (= `win-x64_net` do GitHub, .NET embutido). O zip maior do GitHub depende do .NET 10 instalado no prefixo e não abre. O Pandora 5.0.0-beta ignora `-o`: a saída vai para o `Data` virtual, ou seja, para `overwrite/` (o que é correto no MO2). Ele registra "Not launched from a known Mod Manager" (detecta o MO2 pelo processo pai, que no Proton é o `steam.exe`); sem efeito observado. |

Causa do único defeito do MO2: no Windows, `adjustForVirtualized()` (`processrunner.cpp`) roda um programa cujo binário ou pasta de trabalho está em `mods/` a partir do lugar correspondente no `Data` virtual (`mods\Nemesis\Nemesis_Engine\x.exe` → `<jogo>\Data\Nemesis_Engine\x.exe`), relançando o MO2 com o comando `launch` para o novo processo ser enganchado pelo USVFS. No Linux isso está desligado: o programa roda de onde está (comentário do topo de `processrunner_linux.cpp`).

Também observado: em duas execuções pelo comando `run` com o log no nível normal, o MO2 parou de esperar o programa antes de ele terminar ("mo2-vfs is still running, left to end by itself"); com o log em debug, três execuções esperaram certo. Por decisão do usuário fica fora desta fase, registrado como pendência (§6).

### 1.2 Decisões desta fase

| Tema | Decisão |
|---|---|
| Validação do xEdit de outros jogos (decisão do usuário, 2026-10-21) | Só o Skyrim SE está instalado: BodySlide, Nemesis, Pandora e SSEEdit são testados de verdade no Skyrim SE; o xEdit dos outros jogos é coberto por testes automáticos com fixtures (§3), sem teste manual real. |
| Execução virtualizada (abordagem A, decisão do usuário) | Reescrever o binário e a pasta de trabalho para o `Data` antes do `spawn`, sem relançar o MO2: o mount FUSE vale para o sistema todo, então não há o que enganchar. Descartadas: só a pasta de trabalho (não resolve o Nemesis, que usa o caminho do `.exe`); opção por executável (diverge do Windows e exige que o usuário saiba). |
| Mod desativado | Se o mod do binário estiver desativado no perfil da execução, o arquivo não existe no `Data` virtual: o programa roda do caminho real (comportamento de hoje), com um aviso no log. No Windows, nesse caso, o lançamento falha. |
| Regra dos caminhos | Uma só, compartilhada com o Windows (§2.1); o `adjustForVirtualized()` do Windows passa a usá-la e não muda de comportamento. |
| Patches | `deps/modorganizer: Linux: …` e `deps/game_bethesda: Linux: …`, como nas fases anteriores; código Windows idêntico ou em `#ifdef _WIN32`. |

## 2. Execução virtualizada (`deps/modorganizer`)

### 2.1 Regra dos caminhos (arquivo novo, comum às duas plataformas)

`virtualizedPaths(modsPath, dataPath, binary, cwd)` → `std::optional<VirtualizedPaths>` com `{binary, cwd, mod}`:

- `binary` e `cwd` são avaliados separadamente, como hoje: um caminho começa em `mods/` quando começa com `modsPath` + `/`, sem diferenciar maiúsculas (a comparação atual do Windows).
- `mods/<mod>/resto` → `<data>/resto`; `mods/<mod>` sem o resto → `<data>`.
- `mod` é o nome do mod do **binário** (vazio se só a pasta de trabalho está em `mods/`).
- Nenhum dos dois em `mods/` → `std::nullopt`.
- Função pura, sem acesso ao disco nem às configurações; arquivo novo sem `#ifdef` (por exemplo, `virtualizedpaths.h/.cpp`), compilado nas duas plataformas.

O `adjustForVirtualized()` do Windows passa a obter os dois caminhos dela e continua montando o mesmo `launch "<cwd>" "<binário>" <argumentos>` com o binário do próprio MO2.

### 2.2 Uso no Linux (`ProcessRunner::run()`, `src/linux/processrunner_linux.cpp`)

Só nas execuções com VFS (`m_sp.hooked`); sem VFS e documentos abertos pela aplicação associada (`setFromFile` com arquivo que não é programa) não mudam.

1. Calcula `virtualizedPaths()` com `settings().paths().mods()`, `managedGame()->dataDirectory()`, `m_sp.binary` e `m_sp.currentDirectory`.
2. Se o binário está em `mods/<mod>/…` e `<mod>` está **ativo no perfil da execução** (o de `m_profileName`, ou o perfil atual quando ele está vazio), o lançamento usa o binário e a pasta de trabalho reescritos. Se `<mod>` está desativado ou não existe, o binário fica o real e o log recebe um aviso (`"'<mod>' is not active; '<binário>' runs from the mods directory"`); a pasta de trabalho é reescrita só se o binário também foi (para não juntar um binário real com um `cwd` virtual). Só a pasta de trabalho em `mods/` (binário fora) → reescrita da pasta de trabalho, como no Windows.
3. `proton::makeLaunch()` recebe os caminhos do lançamento. Ele decide `.exe`/`.bat`/`.cmd`/`.com` → `umu-run` pela extensão, sem abrir o arquivo; o nome do log da saída (`run-<nome>.log`) não muda, porque o nome do arquivo é o mesmo.
4. `OrganizerCore::beforeRun()` continua recebendo o binário, a pasta e os argumentos **originais** de `m_sp`: no Windows a reescrita acontece depois do `beforeRun`, então os plugins (`onAboutToRun`) veem o mesmo caminho nas duas plataformas.
5. `VfsConnector::spawn()` como hoje. O Wine só abre o binário depois do mount (verificado no spike).
6. Log de debug: `"virtualized: '<real>' runs as '<no Data>' in '<cwd>'"`.

O comentário do topo de `processrunner_linux.cpp` passa a descrever isso no lugar de "it runs from where it is".

### 2.3 Erros

Se o arquivo reescrito não existir no `Data` montado (por exemplo, um mod de prioridade maior tem uma pasta no lugar), o `umu-run`/Wine falha e a saída vai para o `run-<nome>.log`, como qualquer programa que não abre hoje. Não há diagnóstico extra antes do mount.

## 3. xEdit de outros jogos: fixtures (`deps/game_bethesda/tests`)

Hoje `game_bethesda_tests` carrega só o `game_skyrimse` pelo `QPluginLoader`. Um teste parametrizado novo segue o mesmo padrão (biblioteca da Steam falsa em pasta temporária, `appmanifest_<appid>.acf`, `compatdata/<appid>/pfx/drive_c/users/steamuser`) para cada jogo da Steam que o xEdit suporta:

| Jogo | Plugin | AppID |
|---|---|---|
| Skyrim | `game_skyrim` | 72850 |
| Skyrim VR | `game_skyrimvr` | 611670 |
| Enderal | `game_enderal` | 933480 |
| Enderal SE | `game_enderalse` | 976620 |
| Oblivion | `game_oblivion` | 22330 |
| Fallout 3 | `game_fallout3` | 22300 (e a GOTY, 22370, se o plugin a detectar pela Steam) |
| Fallout NV | `game_falloutnv` | 22380 |
| Fallout 4 | `game_fallout4` | 377160 |
| Fallout 4 VR | `game_fallout4vr` | 611660 |
| Starfield | `game_starfield` | 1716740 |

Para cada um:
- o jogo é detectado na biblioteca falsa (`isInstalled()`, `gameDirectory()`);
- `documentsDirectory()` e `savesDirectory()` ficam em `pfx/drive_c/users/steamuser/Documents/My Games/<pasta do jogo>` do prefixo **daquele appid** (onde o xEdit lê os INIs);
- os mapeamentos de `IPluginFileMapper::mappings()` com destino no `AppData/Local` (o `plugins.txt`/`loadorder.txt` do perfil, que o xEdit lê de `%LOCALAPPDATA%\<jogo>`) apontam para `pfx/drive_c/users/steamuser/AppData/Local/<pasta do jogo>` do mesmo prefixo.

Os nomes de pasta esperados (`My Games/<…>`, `AppData/Local/<…>`) são os do Windows para cada jogo e ficam numa tabela no teste. Esses plugins hoje só compilam no Linux: se um teste falhar (por exemplo, um jogo que sobrescreve `documentsDirectory()` sem olhar o prefixo), a correção entra nesta fase, como patch `deps/game_bethesda: Linux: …` no ponto comum (`GameGamebryo`) quando possível.

Fora: Morrowind (o xEdit não o suporta), TTW e Nehrim (rodam sobre o FNV e o Oblivion), Fallout 4 London (instalação sobre o FO4), Fallout 76 (sem plugins locais).

O que o xEdit lê do registro do prefixo (`Bethesda Softworks\<jogo>\Installed Path`) não é responsabilidade do MO2: a Steam o grava quando o jogo roda pela primeira vez; a documentação diz isso (§4).

## 4. Documentação

- **README, seção "Ferramentas"** (configuração no Linux):
  - BodySlide, Nemesis e Pandora instalados como mod e configurados como executável pelo `.exe` dentro do mod; o MO2 os roda de dentro do `Data` virtual, como no Windows.
  - Pandora: usar o build do Nexus (`win-x64_net` no GitHub), que traz o .NET; o build que depende do .NET não abre num prefixo sem .NET 10. Na 5.0.0-beta o `-o` é ignorado e a saída vai para o `overwrite` (mover para um mod depois, como no Windows).
  - xEdit: o executável precisa estar no prefixo do jogo (campo "Steam AppID" do executável, se for outro) e o jogo precisa ter rodado uma vez pela Steam (registro `Installed Path` e pastas do prefixo).
- **Checklist manual** `docs/testing/phase4c3-manual-checklist.md`: os três fluxos do spike, agora configurados pelo caminho do mod (Nemesis Update + Launch; BodySlide abre e acha o `Data`; Pandora gera a saída), saída no `overwrite/`, mount desmontado no fim; o mod desativado roda do caminho real com o aviso no log; SSEEdit continua funcionando.
- **ROADMAP.md e STATE.md:** fase, decisões, limitações e a pendência do §6.

## 5. Testes

- **Unitários (`modorganizer`, gtest):** `virtualizedPaths()` com o binário em `mods/`, só a pasta de trabalho, os dois, nenhum, `mods/<mod>` sem subpasta, `mods/` com maiúsculas diferentes e um prefixo parecido mas diferente (`mods2/`, que não conta).
- **e2e (`tests/vfs-e2e`):** um `.exe` dentro de `mods/<mod>/Tool/` com o `umu-run` falso da Fase 3 (`MO2_VFS_BRIDGE=direct`). O falso registra argv e cwd, lê um arquivo vizinho pelo caminho do `Data` montado e cria um arquivo novo. Esperado: argv e cwd em `<jogo>/Data/Tool/…`, a leitura funciona, o arquivo novo vai para `overwrite/` e o mount cai no fim. Segundo caso, com o mod desativado: argv no caminho real e o aviso no log.
- **game_bethesda:** o teste parametrizado do §3.
- **CI:** Linux pelo `build.sh --tests`; Windows pelo `[windows-ci]`, que só compila (o `adjustForVirtualized()` mudou de forma, não de comportamento; a equivalência foi conferida linha a linha na revisão).
- **Manual:** checklist do §4 no Bazzite.

## 6. Pendências registradas (fora desta fase)

- O comando `run` às vezes para de esperar o programa antes de ele terminar (visto duas vezes com o log no nível normal, nunca em debug; o MO2 sai com "left to end by itself" e o mount fica até o programa sair). Investigar numa fase futura; registrado no STATE.md.
- Pandora "Not launched from a known Mod Manager" no Proton: comportamento do Pandora; sem efeito observado.

## 7. Riscos

| Risco | Mitigação |
|---|---|
| A refatoração do `adjustForVirtualized()` muda o Windows | Função pura com testes unitários, que rodam só no Linux (o `organizer_tests` não é compilado no Windows); o CI do Windows apenas compila, e a equivalência do `adjustForVirtualized()` com o código anterior foi conferida linha a linha na revisão; a montagem do `launch` não muda. |
| Ferramenta que depende de estar fora do `Data` | É o mesmo comportamento do MO2 no Windows; se aparecer, o usuário pode configurar o executável fora de `mods/`. |
| Plugins de jogo que só compilam têm defeitos de caminho no Linux | É o que os testes do §3 procuram; a correção entra nesta fase. |
| O mod do binário é ativo, mas outro mod de maior prioridade cobre o arquivo | O executável vencedor do `Data` é o que roda, como no Windows. |

## 8. Critérios de pronto

- Um executável configurado pelo caminho do `.exe` dentro de um mod ativo roda do `Data` virtual no Linux; com o mod desativado, do caminho real com aviso no log.
- O Windows continua com o mesmo comportamento (CI verde).
- Os testes parametrizados do §3 passam para os dez jogos.
- Unitários, e2e, smoke e loot-e2e verdes no `./build.sh --tests`; CI Linux e Windows verdes.
- Teste manual no Bazzite aprovado (Nemesis, BodySlide, Pandora e SSEEdit no Skyrim SE).
- README, checklist, ROADMAP e STATE atualizados.
