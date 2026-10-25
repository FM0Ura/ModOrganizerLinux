# Fase 6a — Suporte ao Stellar Blade — Design

- **Data:** 2026-10-25
- **Status:** em revisão
- **Spec geral:** [`2026-10-14-mo2-linux-port-design.md`](2026-10-14-mo2-linux-port-design.md)
- **Fases relacionadas:** 4d.2 (`basic_games` no Linux, [`2026-10-21-phase4d2-basic-games-design.md`](2026-10-21-phase4d2-basic-games-design.md)), que deixou o Stellar Blade "para depois"; 6b (Nexus Collections, [`2026-10-25-phase6b-nexus-collections-design.md`](2026-10-25-phase6b-nexus-collections-design.md)), que usa o Stellar Blade como jogo-alvo.
- **Divisão (decisão do usuário, 2026-10-25):** duas fases separadas — primeiro o Stellar Blade (esta), depois as Collections.
- **Numeração e branch (decisão do Claude):** "Fase 6a/6b", depois da Fase 5 (Distribuição, ainda em andamento). Branch `fase-6a-stellar-blade` a partir de `origin/main`, no worktree `../ModOrganizerLinux-6a`; o plano decide se rebaseia sobre `fase-5-distribuicao` quando ela entrar no `main`.

## 1. Objetivo e porquê

O usuário quer gerenciar os mods do Stellar Blade pelo MO2 no Linux, como já faz com Skyrim SE, Cyberpunk e NieR. Hoje o MO2 não conhece o jogo (não há plugin no upstream do `basic_games`). Os mods do Stellar Blade vão para vários lugares diferentes na pasta do jogo, e errar o lugar faz o mod simplesmente não carregar — por isso o centro desta fase é **instalar cada tipo de mod no lugar certo, sem o usuário mover arquivos à mão**.

Meta: com o Stellar Blade da Steam do usuário (Bazzite), criar uma instância no MO2, instalar mods `.pak` comuns, o UE4SS, um mod Lua do UE4SS e um mod de `LogicMods`, e jogar pelo MO2 com todos carregando.

### 1.1 Metas

- Detectar o jogo pela Steam (app ID `3489700`, pasta `steamapps/common/StellarBlade`) e iniciá-lo pelo Proton/VFS como os outros jogos do `basic_games`.
- Domínio Nexus `stellarblade` (downloads `nxm://` e checagem de atualização funcionam).
- Arrumação automática dos mods nos quatro destinos (§3).
- Saves e configs por perfil, se o VFS permitir (§4).

### 1.2 Não-metas

- Editor de ordem de `.pak` (aba "Paks" como a do Oblivion Remaster): a prioridade do MO2 já decide conflitos de arquivo (decisão do usuário: "acredito que esteja bom").
- Gerenciar a lista de mods ativos do UE4SS por aba própria (como a aba UE4SS do Oblivion Remaster).
- GOG/Epic (fora de escopo do projeto).
- Nexus Collections (Fase 6b).

### 1.3 Anti-metas (o que tornaria isto um fracasso mesmo "funcionando")

- Um mod instalado que **não carrega** no jogo porque foi parar na pasta errada sem o MO2 avisar. Exceção conhecida e aceita: um mod de `LogicMods` empacotado sem a pasta `LogicMods` (§3.1, regra 2) — o arquivo sozinho não permite distingui-lo de um `.pak` comum.
- O MO2 "consertar" um mod que já vinha certo e quebrá-lo (ex.: tirar um `.pak` de `LogicMods` e jogar em `~mods`).

## 2. Dados do jogo (verificados na instalação do usuário, 2026-10-25)

| Item | Valor |
|---|---|
| Steam app ID | `3489700` |
| Pasta | `steamapps/common/StellarBlade` |
| Motor | Unreal Engine 4 (`WindowsNoEditor`, IoStore: `.pak` + `.ucas` + `.utoc`) |
| Lançador | `SB.exe` (raiz) |
| Executável real | `SB/Binaries/Win64/SB-Win64-Shipping.exe` |
| Paks do jogo | `SB/Content/Paks` |
| Saves | `<prefixo>/drive_c/users/steamuser/AppData/Local/SB/Saved/SaveGames/<steamid64>/*.sav` (verificado; `Documents/StellarBlade/<steamid64>` só tem o `steam_autocloud.vdf`) |
| Configs | `<prefixo>/drive_c/users/steamuser/AppData/Local/SB/Saved/Config/WindowsNoEditor/` (`Engine.ini`, `Game.ini`, `Input.ini`, `Compat.ini`, … — verificado) |
| Domínio Nexus | `stellarblade` |

## 3. Destinos de mods e arrumação na instalação

Destinos confirmados pelo usuário (são esses e só esses):

| # | Tipo | Destino (relativo à raiz do jogo) |
|---|---|---|
| 1 | Mod `.pak` comum (`.pak`/`.ucas`/`.utoc`) | `SB/Content/Paks/~mods/` |
| 2 | Mod de blueprint (UE4SS BPModLoader) | `SB/Content/Paks/LogicMods/` |
| 3 | UE4SS (DLLs, `UE4SS-settings.ini`, pasta `ue4ss/`) e mods Lua/C++ do UE4SS | `SB/Binaries/Win64/` e `SB/Binaries/Win64/ue4ss/Mods/<mod>/` (ou `SB/Binaries/Win64/Mods/<mod>/`, conforme o layout do build do UE4SS — §3.2) |
| 4 | Configs `.ini` do jogo | `AppData/Local/SB/...` no prefixo (fora da pasta do jogo — §4) |

**Modelo (decisão do Claude, aceita pelo usuário):** a pasta de dados do MO2 é a raiz do jogo (`GameDataPath = "%GAME_PATH%"`, como o Silent Hill 2 Remake). Cada mod reproduz a estrutura da raiz (`SB/Content/Paks/~mods/...`). Assim os destinos 1–3 cabem num só modelo e os conflitos aparecem no painel do MO2.

### 3.1 Regras do verificador de dados (`ModDataChecker`)

Decisões do Claude, aceitas pelo usuário; os detalhes de casamento de nomes ficam com o construtor:

1. **Já estruturado:** se o arquivo do mod contém um caminho que casa com um sufixo da raiz (`SB/…`, `Content/Paks/…`, `Paks/…`, `~mods/…`, `LogicMods/…`, `Binaries/Win64/…`, `Win64/…`, `ue4ss/Mods/…`, `Mods/<mod>/Scripts/…`), o MO2 completa o prefixo que falta e **não reclassifica** o conteúdo (anti-meta §1.3). Comparação sem diferenciar maiúsculas; o destino usa a grafia canônica (`~mods`, `LogicMods`).
2. **`.pak`/`.ucas`/`.utoc` soltos** (na raiz do arquivo ou numa única pasta de embrulho): vão para `SB/Content/Paks/~mods/`, sem aviso. Mods de `LogicMods` normalmente já vêm dentro da pasta `LogicMods` (regra 1); se não vierem, o usuário move pelo MO2 (aceito pelo usuário). **Decisão do Claude:** sem heurística nem diálogo para adivinhar `LogicMods`; o README explica o caso.
3. **Mod do UE4SS** (pasta com `Scripts/main.lua`, `dlls/main.dll` ou `enabled.txt`): vai para a pasta de mods do UE4SS, em `<destino>/<nome-da-pasta>/`.
4. **O próprio UE4SS** (`UE4SS.dll` e a DLL proxy, `UE4SS-settings.ini`): vai para `SB/Binaries/Win64/`.
5. **Lixo** (`*.txt` de readme, `*.md`, imagens de capa, `manifest.json`): descartado, como no Silent Hill 2 Remake — mas **não** `enabled.txt` nem `.txt` dentro de pastas de mod do UE4SS.
6. Nada reconhecível: `INVALID` (o MO2 abre a instalação manual como hoje).

### 3.2 UE4SS real (verificado em 2026-10-25 pela prévia de conteúdo pública da Nexus)

- O UE4SS usado pela Collection de referência é o **"Stellar Blade UE4SS"** ([mod 2952](https://www.nexusmods.com/stellarblade/mods/2952), arquivo "SB UE4SS 1.3"). Layout: `SB/Binaries/Win64/dwmapi.dll` + `SB/Binaries/Win64/ue4ss/` (com `Mods/<mod>/Scripts/main.lua`, `Mods/mods.txt`, `Mods/mods.json`, `Mods/shared/`). Logo, pasta de mods do UE4SS = `SB/Binaries/Win64/ue4ss/Mods`; DLL proxy = `dwmapi.dll`.
- Layouts reais de mods: CNS (mod 1496) já vem com `SB/...` completo, misturando `ue4ss/Mods/DekCNS/` (com `enabled.txt`), `ue4ss/UE4SS-settings.ini`, `Paks/LogicMods/*.pak` e `Paks/~mods/CustomNanosuitSystem/*.json`; Better Jiggle (mod 1570) vem com `ue4ss/Mods/JiggleUpdate/` + `ue4ss/Mods/mods.txt` + `ue4ss/UE4SS-settings.ini` na raiz; Catwalk (mod 2717) vem com `~Mods/` na raiz (com `.pak` e `.json`); o Chunk ID Patcher (mod 861) é só um `.exe` na raiz.
- **Problema achado:** vários mods trazem o próprio `ue4ss/Mods/mods.txt`; o de prioridade maior substituiria a lista do UE4SS e poderia desligar o `BPModLoaderMod` (sem ele, nenhum mod de `LogicMods` carrega) — anti-meta §1.3. **Decisão do Claude** (no espaço que esta seção deixava ao construtor), no padrão do Oblivion Remaster: o plugin **gera** `mods.txt` e `mods.json` no perfil, a cada execução, e os mapeia para `SB/Binaries/Win64/ue4ss/Mods/`; os `mods.txt`/`mods.json` dos mods ficam sem efeito. A lista gerada: os mods padrão do UE4SS com os valores padrão, mais `<pasta> : 1` para cada outra pasta de `ue4ss/Mods/` presente nos mods ativos (exceto `shared`). Sem aba de edição nesta fase (não-meta §1.2).
- `WINEDLLOVERRIDES=dwmapi=n,b`: o `basic_games` não tem como declarar o ambiente do executável, então segue o precedente da 4d.2 (README e checklist).
- `UE4SS-settings.ini` trazido por vários mods (CNS, Jiggle) fica com a prioridade do MO2 decidindo, com o conflito visível no painel.
- `.exe` solto (Chunk ID Patcher): `INVALID` pela regra 6; o usuário decide (é uma ferramenta, não um mod).

### 3.2.1 (original) A verificar pelo construtor no UE4SS real

- Qual build do UE4SS a comunidade usa hoje no Stellar Blade (Nexus [mod 378](https://www.nexusmods.com/stellarblade/mods/378) e o fork mencionado lá) e o layout que ele espera: `Win64/ue4ss/Mods` (UE4SS ≥ 3.0) ou `Win64/Mods` (antigo). A regra 3 segue esse layout.
- Qual DLL proxy ele usa (`dwmapi.dll`, `xinput1_3.dll`, …). No Linux, DLL proxy só carrega com `WINEDLLOVERRIDES=<dll>=n,b` (precedente da 4d.2: CET/RED4ext, FAR). **Decisão do Claude:** se o `basic_games` permitir declarar o ambiente do executável, o plugin já o declara; senão, segue o precedente — README e checklist explicam a linha em **Modify Executables → Proton → Environment**.
- Como o UE4SS decide quais mods carregam (`Mods/mods.txt`, `mods.json`, `enabled.txt`). Se precisar de `mods.txt` listando o mod, o construtor decide entre o verificador criar `enabled.txt` no mod instalado ou documentar; não pode ficar um mod instalado que não carrega sem aviso (anti-meta).

### 3.3 Relação com a Fase 6b (decisão do Claude)

A 6a é planejada sozinha: o verificador segue as regras acima, não as pastas por tipo de mod do Vortex. A tradução do layout do Vortex (caminhos de FOMOD e de arquivos do autor relativos à pasta do tipo de mod) fica com a 6b. Reaproveitar a lógica de UE4SS de `games/oblivion_remaster/mod_data_checker.py` quando servir.

## 4. Saves e configs

- **Saves:** `GameSavesDirectory` = `%USERPROFILE%/AppData/Local/SB/Saved/SaveGames`, extensão `sav` (a aba de saves os lista). **Decisão do Claude:** sem saves locais por perfil nesta fase — os saves ficam numa subpasta por SteamID e sincronizam pela Steam Cloud (`steam_autocloud.vdf`), e trocar a pasta por perfil arrisca brigar com a nuvem.
- **Configs por perfil:** os `.ini` de `AppData/Local/SB/Saved/Config/WindowsNoEditor` (`Engine.ini`, `Game.ini`, `Input.ini`, `GameUserSettings.ini`) como arquivos do perfil, mapeados para o prefixo. A 4d.2 já mapeia arquivo de perfil para o prefixo (o `modsettings.lsx` do BG3) e o VFS já grava na pasta real quando a raiz só tem mappings de arquivo (Fase 3), então deve funcionar. **Se não funcionar no teste, os configs ficam de fora nesta fase** (decisão do usuário) — registrado como limitação.

## 5. Onde fica o código

- `deps/basic_games/games/game_stellarblade.py` (e, se crescer, um pacote `games/stellarblade/` como o do Oblivion Remaster), no estilo do upstream. É arquivo novo nosso: o cabeçalho e o estilo seguem os outros jogos, não precisa do `if sys.platform` (não há o que diferir do Windows, a não ser a nota de `WINEDLLOVERRIDES`).
- Nada no C++ nem no `mo2-vfs`, a não ser que §3.2/§4 mostrem um buraco; nesse caso o construtor para e reporta antes de mexer no núcleo.

## 6. Testes

- **Automáticos:** testes Python do verificador (padrão dos testes do `basic_games` da 4d.2) com árvores de arquivo falsas: `.pak` solto, mod já em `~mods`, mod em `LogicMods`, mod com `SB/Content/Paks/...` completo, UE4SS completo, mod Lua do UE4SS, arquivo com pasta de embrulho, lixo, nada reconhecível. Mais: detecção pelo app ID com a Steam falsa da 4d.2.
- **Manual no Bazzite** (`docs/testing/phase6a-manual-checklist.md`). Pré-condição: a pasta real do jogo está limpa — verificado em 2026-10-25 que não há `~mods`, `LogicMods` nem UE4SS instalados à mão em `SB/Content/Paks` e `SB/Binaries/Win64`; o checklist manda conferir de novo antes do teste. Passos: criar a instância; instalar 1 mod `.pak` (cor de cabelo/roupa visível), o UE4SS, 1 mod Lua e 1 mod `LogicMods`; jogar pelo MO2 e ver os quatro carregando; saves e (se §4 vingar) um `.ini` alterado no perfil chegando ao jogo; desativar um mod e ver que some.

## 7. Fica com o construtor

- Nomes de classes, o casamento exato de sufixos e de pastas de embrulho, a ordem das regras.
- Ícone, `GameSupportURL`, metadados do plugin.
- Se o verificador vira pacote ou arquivo único.
- A pasta exata dos saves e dos `.ini` (a partir do prefixo real).
