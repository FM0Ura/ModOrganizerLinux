# Fase 4d.2 — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-21-phase4d2-basic-games-design.md`](../superpowers/specs/2026-10-21-phase4d2-basic-games-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`. Pacotes do box: `python3 python3-pyqt6` (README, "Plugins Python"); o `basic_games` não pede nada além disso. Jogos da Steam nativa do usuário, cada um **rodado ao menos uma vez pela Steam** (o prefixo do Proton tem de existir): NieR:Automata (524220), Cyberpunk 2077 (1091500) e Baldur's Gate 3 (1086940).

**Build:** o `install/` do worktree `ModOrganizerLinux-4d2` (`./build.sh` na distrobox `mo2-run`) ou, depois de um run Linux verde do PR, o artefato `mo2-linux-install` desse run (restaure a permissão de execução de `bin/ModOrganizer` e `bin/mo2-vfs`).

`<prefixo>` abaixo é `<biblioteca da Steam do jogo>/steamapps/compatdata/<app ID>/pfx/drive_c/users/steamuser`.

## 1. Plugin carregado

- [ ] Abrir o MO2. Esperado: nenhum aviso do "Python Proxy" no ícone de notificações; em **Configurações → Plugins**, aparecem o "Python Proxy" e os plugins do `basic_games`: "NieR:Automata Support Plugin", "Cyberpunk 2077 Support Plugin" e "Baldur's Gate 3 Plugin" (mais os "BG3 Tools: …", as três ferramentas do BG3).
- [ ] No log (`logs/mo_interface.log`): nenhuma linha `failed to init python`; uma linha por loja ausente, cada uma uma só vez (não repetida a cada consulta): `GOG: only Steam games are detected on Linux`, `Epic Games: only Steam games are detected on Linux`, `EA Desktop: only Steam games are detected on Linux` e `Origin: only Steam games are detected on Linux`.

## 2. NieR:Automata

- [ ] **Nova instância**: o NieR:Automata aparece na lista de jogos detectados (com o caminho da Steam); criar a instância.
- [ ] Instalar um mod simples cujos arquivos vão na raiz do jogo (uma pasta com um `.txt` ou `.dll` de teste) e ativá-lo. Esperado: o mod aparece na lista e fica ativo.
- [ ] Escolher o executável do jogo (a lista de executáveis traz o `NieRAutomata.exe`, o binário do plugin) e **executar pelo MO2** (Proton com o VFS). Esperado: o jogo abre; com o jogo aberto, `ls "<pasta do jogo>"` (num terminal do host) mostra o arquivo do mod na raiz do jogo (o `logs/run-NieRAutomata.log` só traz a saída do Proton e não serve para isso; um mod funcional também vale); depois de fechar o jogo, o arquivo some da pasta (o mount some).

## 3. Cyberpunk 2077

- [ ] **Nova instância**: o Cyberpunk 2077 aparece na lista; criar a instância.
- [ ] Documentos e saves (o MO2 não mostra essas pastas numa tela; confira pelos saves): se o jogo já tem saves, a aba **Saves** da janela principal lista os mesmos arquivos que `ls "<prefixo>/Saved Games/CD Projekt Red/Cyberpunk 2077"` (`<prefixo>` com o compatdata `1091500`) e que nada é lido de `~/Documents` ou de pastas XDG. Sem saves, a aba fica vazia e o item vale só como "sem aviso": o log (`logs/mo_interface.log`) não tem a linha `Cyberpunk 2077: no Proton prefix (run the game once from Steam); using the folders of Linux for its documents and saves` (que só aparece quando o jogo nunca rodou pelo Proton e as pastas caem no XDG).
- [ ] Instalar um mod de arquivo (com `archive/pc/mod/…`) e ativá-lo; executar o jogo pelo MO2. Esperado: o jogo abre e o mod é visto.
- [ ] Opcional, REDmod: instalar um mod REDmod (pasta em `mods/`) e executar o jogo pelo executável "Cyberpunk 2077 (REDmod)" (o padrão: faz o deploy do REDmod, se for preciso, e inicia o jogo com `-modded`; com a configuração do plugin "Deploy redmod before game launch if necessary", ligada por padrão). Para só fazer o deploy, execute o executável "REDmod" (argumentos `deploy -reportProgress -force %modlist%`). Esperado: o `redMod.exe` roda por Proton e encontra a lista (`-modlist="Z:\…"`): sem erro de arquivo não encontrado em `logs/run-redMod.log` nem em `<pasta do jogo>/tools/redmod/bin/REDmodLog.txt`.

## 4. Baldur's Gate 3

- [ ] **Nova instância**: o Baldur's Gate 3 aparece na lista; criar a instância. Os documentos ficam em `<prefixo>/AppData/Local/Larian Studios/Baldur's Gate 3` (compatdata `1086940`).
- [ ] Instalar um mod `.pak` do Nexus (pelo `nxm://` ou arquivo). Esperado: ao terminar a instalação aparece **uma vez por sessão** o diálogo de aviso "Baldur's Gate 3 Plugin - Load order" ("The load order (modsettings.lsx) is not generated on Linux yet: LSLib's Divine tool does not accept Unix paths. …"); o mod é instalado em `mods/` sem erro, nada é baixado do GitHub nem executado (o `Divine.dll` não roda) e o `logs/mo_interface.log` tem uma única linha de aviso `Baldur's Gate 3: the load order (modsettings.lsx) is not generated on Linux: LSLib's Divine does not accept Unix paths; the rest of the plugin works`. Ativar ou mover o mod não mostra nada.
- [ ] Instalar um segundo `.pak`. Esperado: nenhum segundo diálogo na mesma sessão (a linha do log também não se repete); o MO2 não trava. Fechando e abrindo o MO2, o aviso volta uma vez, no primeiro gatilho da nova sessão (uma instalação, a execução do jogo ou uma ferramenta do BG3), e depois não se repete.
- [ ] Abrir **Ferramentas → BG3 Tools → Reparse Pak Metadata** e **Check For LsLib Updates**, mais de uma vez. Esperado: estas ferramentas mostram o diálogo de aviso **toda vez** que são usadas (mesmo já tendo avisado na sessão) e registram a linha no log a cada uso; nada é baixado nem executado e o MO2 não trava.
- [ ] Executar o jogo pelo MO2 (o executável do BG3 em `bin/bg3.exe`). Esperado: o jogo abre; o diálogo de aviso aparece só se este for o primeiro gatilho da sessão (se já apareceu nesta sessão, não há novo diálogo); sem o `modsettings.lsx` gerado, os mods não são aplicados pelo MO2.
- [ ] Opcional: colocar um `modsettings.lsx` feito à mão no perfil (`profiles/<perfil>/modsettings.lsx`) e executar o jogo pelo MO2. Esperado: o arquivo chega ao jogo em `<prefixo>/AppData/Local/Larian Studios/Baldur's Gate 3/PlayerProfiles/Public/modsettings.lsx`.

## 5. DLL proxy com `WINEDLLOVERRIDES` (opcional)

- [ ] Mods que trazem uma DLL proxy (CET e RED4ext no Cyberpunk 2077, `version.dll`/`winmm.dll`; Script Extender do BG3, `DWrite.dll`; FAR no NieR:Automata, `dinput8.dll`) não carregam sozinhos no Linux: as bibliotecas forçadas são ignoradas e as opções de inicialização da Steam não valem. Em **Tools → Modify Executables…**, selecionar o executável do jogo e, no grupo **Proton**, na linha **Environment** (uma variável por linha), escrever `WINEDLLOVERRIDES=version,winmm=n,b` (Cyberpunk 2077), `WINEDLLOVERRIDES=DWrite=n,b` (BG3) ou `WINEDLLOVERRIDES=dinput8=n,b` (NieR:Automata); **OK**. Instalar o mod correspondente e executar o jogo pelo MO2. Esperado: o mod carrega (por exemplo, o CET abre a janela dele no Cyberpunk; o Script Extender cria o log dele no BG3); sem a variável, o mod não faz nada. Anotar o resultado: este passo valida o texto do README e do STATE.

## 6. Outros jogos (opcional)

- [ ] Um jogo do `basic_games` que o usuário tenha na Steam além dos três acima: a instância é criada e o jogo é detectado. Estes jogos são "detectados, não validados" (spec, limitações); anotar o que falhar.

## 7. Regressão

- [ ] Instância de Skyrim SE: SKSE inicia o jogo com os mods (Fase 3); os plugins Python da Fase 4d.1 (itens 1 a 5 do [checklist da 4d.1](phase4d1-manual-checklist.md)) continuam funcionando; LOOT ordena (4b).
- [ ] Fechar o MO2 pelo menu (**Arquivo → Sair**): fecha sem travar e o `logs/crash.log` da instância continua vazio.
