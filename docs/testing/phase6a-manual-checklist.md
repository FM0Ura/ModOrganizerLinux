# Fase 6a — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-25-phase6a-stellar-blade-design.md`](../superpowers/specs/2026-10-25-phase6a-stellar-blade-design.md) · Plano: [`../superpowers/plans/2026-10-25-phase6a-stellar-blade.md`](../superpowers/plans/2026-10-25-phase6a-stellar-blade.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`, com o `install/` do worktree `ModOrganizerLinux-6a` (`./build.sh` na distrobox `mo2-run`). Pacotes do box: `python3 python3-pyqt6` ([docs/dev/building.md](../dev/building.md#python-no-box)). Stellar Blade (Steam, app 3489700) **rodado ao menos uma vez pela Steam** (o prefixo do Proton tem de existir). Downloads pelo botão **Download with mod manager** do site da Nexus (handler `nxm://` da Fase 4a).

`<prefixo>` abaixo é `<biblioteca da Steam do jogo>/steamapps/compatdata/3489700/pfx/drive_c/users/steamuser`.

## 0. Pré-condição

- [ ] `ls <pasta do jogo>/SB/Content/Paks`: só os paks do jogo (nenhum `~mods` nem `LogicMods`). `ls <pasta do jogo>/SB/Binaries/Win64`: sem `dwmapi.dll` nem `ue4ss`.
- [ ] O Stellar Blade rodou uma vez pela Steam (a pasta `<prefixo>` existe).

## 1. Instância

- [ ] Abrir o MO2 e criar a instância do Stellar Blade (o jogo aparece na lista de detectados, com o caminho da Steam). Esperado: nenhum aviso do "Python Proxy".

## 2. Instalação dos mods

- [ ] Instalar, pelo **Download with mod manager**: Stellar Blade UE4SS (mod 2952), CNS (mod 1496), Better Jiggle (mod 1570), Catwalk Animations (mod 2717) e um mod `.pak` solto com efeito visível (cor de cabelo). Esperado, em cada um: nenhuma janela de instalação manual; na aba **Filetree** do mod, o conteúdo está sob `SB/…` nos destinos da spec §3 (`.pak`/`.ucas`/`.utoc` em `SB/Content/Paks/~mods`; o UE4SS: `dwmapi.dll` em `SB/Binaries/Win64` e a pasta `ue4ss/` em `SB/Binaries/Win64/ue4ss`; mods do UE4SS em `SB/Binaries/Win64/ue4ss/Mods/<mod>`). Anotar o que cair em outro lugar; um mod de `LogicMods` empacotado sem a pasta `LogicMods` cai em `~mods` (limitação conhecida: mover pelo MO2).
- [ ] Na aba **Filetree** do CNS, confirmar que `SB/Content/Paks/LogicMods/DekCNS_P.pak` continua em `LogicMods` (não foi movido para `~mods`).

## 3. DLL proxy do UE4SS

- [ ] **Tools → Modify Executables…**, selecionar o executável **"Stellar Blade"** (`SB.exe`; o BasicGame registra o launcher e o binário com o mesmo título e só o primeiro, o `SB.exe`, sobrevive na lista) e, no grupo **Proton**, na linha **Environment**, escrever `WINEDLLOVERRIDES=dwmapi=n,b`; **OK**.

## 4. Execução

- [ ] Executar o jogo pelo MO2. Esperado: o log do UE4SS (`SB/Binaries/Win64/ue4ss/UE4SS.log`, no `overwrite` ou na pasta real) lista o DekCNS e o JiggleUpdate carregados; o mod `.pak` aparece no jogo; o menu do CNS abre.

## 5. `mods.txt` gerado

- [ ] `<perfil>/mods.txt` tem os mods padrão do UE4SS, `DekCNS : 1`, `JiggleUpdate : 1` e `Keybinds : 1` por último. O `mods.json` do perfil é gerado junto. O `mods.txt` de um mod (se algum trouxer) não vale.

## 6. Desativar mods

- [ ] Desativar o mod `.pak` e executar de novo: o efeito some.
- [ ] Desativar o Better Jiggle: o `JiggleUpdate` some do `mods.txt` (gerado a cada execução) e não carrega.

## 7. Configs por perfil

- [ ] Num perfil com configs locais (a opção **Use profile-specific Game INI Files** fica na janela **Configure Profiles**), editar o `Engine.ini` em **Tools → INI Editor** e executar o jogo. Esperado: o arquivo chega a `<prefixo>/AppData/Local/SB/Saved/Config/WindowsNoEditor/Engine.ini` durante a execução. Se não chegar, anotar (spec §4: os configs ficam de fora e viram limitação).

## 8. Saves

- [ ] A aba **Saves** lista os `.sav` de `<prefixo>/AppData/Local/SB/Saved/SaveGames` (não são por perfil).

## Observações

(Preencher após o teste.)
