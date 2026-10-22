# Fase 4d.2 — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-21-phase4d2-basic-games-design.md`](../superpowers/specs/2026-10-21-phase4d2-basic-games-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`. Pacotes do box: `python3 python3-pyqt6` (README, "Plugins Python"); o `basic_games` não pede nada além disso. Jogos da Steam nativa do usuário, cada um **rodado ao menos uma vez pela Steam** (o prefixo do Proton tem de existir): NieR:Automata (524220), Cyberpunk 2077 (1091500) e Baldur's Gate 3 (1086940).

**Build:** o `install/` do worktree `ModOrganizerLinux-4d2` (`./build.sh` na distrobox `mo2-run`) ou, depois de um run Linux verde do PR, o artefato `mo2-linux-install` desse run (restaure a permissão de execução de `bin/ModOrganizer` e `bin/mo2-vfs`).

`<prefixo>` abaixo é `<biblioteca da Steam do jogo>/steamapps/compatdata/<app ID>/pfx/drive_c/users/steamuser`.

## 1. Plugin carregado

- [ ] Abrir o MO2. Esperado: nenhum aviso do "Python Proxy" no ícone de notificações; em **Configurações → Plugins**, aparecem o "Python Proxy" e os plugins do `basic_games` (por exemplo "Game NieR:Automata", "Game Cyberpunk 2077" e "Baldur's Gate 3 Support Plugin" ou nome parecido).
- [ ] No log (`logs/mo_interface.log`): nenhuma linha `failed to init python`; uma única linha dizendo que só os jogos da Steam são detectados no Linux (GOG, Epic, EA e Origin), não repetida a cada consulta.

## 2. NieR:Automata

- [ ] **Nova instância**: o NieR:Automata aparece na lista de jogos detectados (com o caminho da Steam); criar a instância.
- [ ] Instalar um mod simples cujos arquivos vão na raiz do jogo (uma pasta com um `.txt` ou `.dll` de teste) e ativá-lo. Esperado: o mod aparece na lista e fica ativo.
- [ ] Adicionar o executável do jogo (`NieRAutomata.exe`), se não existir, e **executar pelo MO2** (Proton com o VFS). Esperado: o jogo abre; o arquivo do mod é visto no `Data` virtual (por exemplo pelo log em `logs/run-*.log` ou pelo próprio mod, se for funcional); fechar o jogo e conferir que o mount some.

## 3. Cyberpunk 2077

- [ ] **Nova instância**: o Cyberpunk 2077 aparece na lista; criar a instância.
- [ ] Documentos e saves: **Configurações → Jogo** (ou o botão de pastas) e a aba **Saves**. Esperado: a pasta de saves aponta para `<prefixo>/Saved Games/CD Projekt Red/Cyberpunk 2077` (compatdata `1091500`) e a aba lista os saves do jogo, se houver; nenhuma pasta fora do prefixo (`~/Documents`, por exemplo). Se o prefixo não existisse, o log traria um aviso e as pastas seriam as do XDG.
- [ ] Instalar um mod de arquivo (com `archive/pc/mod/…`) e ativá-lo; executar o jogo pelo MO2. Esperado: o jogo abre e o mod é visto.
- [ ] Opcional, REDmod: instalar um mod REDmod (pasta em `mods/`) e executar o jogo pelo MO2. Esperado: o deploy roda o `redMod.exe` por Proton e o `redMod` encontra a lista (`-modlist=Z:\…`): sem erro de arquivo não encontrado em `logs/run-*.log` e no log do `redMod`.

## 4. Baldur's Gate 3

- [ ] **Nova instância**: o Baldur's Gate 3 aparece na lista; criar a instância. Os documentos ficam em `<prefixo>/AppData/Local/Larian Studios/Baldur's Gate 3` (compatdata `1086940`).
- [ ] Instalar um mod `.pak` do Nexus (pelo `nxm://` ou arquivo) e ativá-lo. Esperado: o mod é instalado em `mods/` sem erro.
- [ ] Pedir a ordem de carga (a ação que antes geraria o `modsettings.lsx`: ativar/mover o mod, ou o menu de ferramentas do BG3). Esperado: um aviso (uma só vez por sessão) dizendo que a ordem de carga do BG3 ainda não é gerada no Linux; o MO2 não trava e nada é baixado do GitHub nem executado (o `Divine.dll` não roda); o log traz o motivo.
- [ ] Executar o jogo pelo MO2. Esperado: o jogo abre (sem o `modsettings.lsx` gerado, os mods não são aplicados pelo MO2).
- [ ] Opcional: colocar um `modsettings.lsx` feito à mão no perfil (`profiles/<perfil>/modsettings.lsx`) e executar o jogo pelo MO2. Esperado: o arquivo chega ao jogo em `<prefixo>/AppData/Local/Larian Studios/Baldur's Gate 3/PlayerProfiles/Public/modsettings.lsx`.

## 5. Outros jogos (opcional)

- [ ] Um jogo do `basic_games` que o usuário tenha na Steam além dos três acima: a instância é criada e o jogo é detectado. Estes jogos são "detectados, não validados" (spec, limitações); anotar o que falhar.

## 6. Regressão

- [ ] Instância de Skyrim SE: SKSE inicia o jogo com os mods (Fase 3); os plugins Python da Fase 4d.1 (itens 1 a 5 do [checklist da 4d.1](phase4d1-manual-checklist.md)) continuam funcionando; LOOT ordena (4b).
- [ ] Fechar o MO2 pelo menu (**Arquivo → Sair**): fecha sem travar e o `logs/crash.log` da instância continua vazio.
