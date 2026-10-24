# Fase 4c.3 — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-21-phase4c3-ferramentas-design.md`](../superpowers/specs/2026-10-21-phase4c3-ferramentas-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`. Instância de Skyrim SE das fases anteriores. Na pasta de downloads da instância: BodySlide and Outfit Studio (Nexus 201), Nemesis Unlimited Behavior Engine (Nexus 60033) e Pandora Behaviour Engine (Nexus 133232).

**Build:** o `install/` do worktree `ModOrganizerLinux-4c3` (`./build.sh` na distrobox `mo2-run`) ou, depois de um run Linux verde do PR, o artefato `mo2-linux-install` desse run (restaure a permissão de execução: `chmod +x <pasta do build>/bin/ModOrganizer <pasta do build>/bin/mo2-vfs`). O `mo2-vfs` precisa ficar na mesma pasta que o `ModOrganizer`.

## Preparação

- [x] Instale os três arquivos como mods (aba Downloads → instalar) e ative-os. Crie um mod vazio "Tool Output" (opcional).
- [x] Configure três executáveis com o binário **dentro da pasta do mod** (botão "..." do binário, navegando até `mods/`):
  - Nemesis: `mods/<Nemesis>/Nemesis_Engine/Nemesis Unlimited Behavior Engine.exe`;
  - BodySlide: `mods/<BodySlide>/CalienteTools/BodySlide/BodySlide.exe` (ou `BodySlide x64.exe`, conforme o arquivo instalado);
  - Pandora: `mods/<Pandora>/.../Pandora Behaviour Engine+.exe`.
- [x] O binário pode ser escolhido tanto por `/home/...` quanto por `/var/home/...` (na distrobox são dois caminhos para a mesma pasta): os dois funcionam e o programa roda de `Data`. Vale testar o Nemesis com cada grafia.
- [x] Anote o conteúdo do `overwrite` (de preferência, vazio).

## 1. Nemesis

- [x] Executar o Nemesis. Esperado: abre sem o "ERROR(1008)"; no log do MO2 (`logs/mo_interface.log`, em debug) há `virtualized: '<...mods...>' runs as '<...>/Data/Nemesis_Engine/...'`.
- [x] "Update Engine" e depois "Launch Nemesis Behavior Engine" terminam sem erro.
- [x] Fechar o Nemesis. Esperado: o MO2 destrava; o `overwrite` tem `meshes/actors/character/behaviors/*.hkx`, `meshes/animationdatasinglefile.txt`, `scripts/FNIS.pex` e `Nemesis_Engine/...`; `findmnt -t fuse.mo2-vfs` no host não lista mais nada.

## 2. BodySlide

- [x] Executar o BodySlide. Esperado: abre, a pré-visualização aparece; em `overwrite/CalienteTools/BodySlide/Log_BS.txt`, "Game data path in config" é `S:\steamapps\common\Skyrim Special Edition\Data\` e "Executable directory" está em `...\Data\CalienteTools\BodySlide`.
- [x] (Opcional, com o CBBE instalado) Batch Build: as malhas vão para o `overwrite/meshes`.

## 3. Pandora

- [x] Executar o Pandora, marcar os patches e clicar em Launch. Esperado: termina; o `overwrite` tem `meshes/actors/character/behaviors/0_master.hkx` (e outros `.hkx`) e `Engine.log`.

## 4. Mod desativado

- [x] Desativar o mod do Nemesis e executá-lo. Esperado: o log do MO2 tem `'<mod>' is not active; '<binário>' runs from the mods directory`; o Nemesis abre e mostra o "ERROR(1008)" (roda da pasta do mod, como antes desta fase). Reativar o mod.
- [ ] (Opcional) Outro perfil: crie um segundo perfil com o mod do Nemesis desativado e, com o perfil aberto tendo-o ativado, rode num terminal `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer run -p <perfil> -e Nemesis`. Esperado: o aviso `'<mod>' is not active; '<binário>' runs from the mods directory` no log.

## 5. Regressão

- [x] SSEEdit (configurado fora de `mods/`) abre pelo VFS e lista os plugins dos mods, como na Fase 3.
- [x] SKSE inicia o jogo com os mods, como na Fase 3.

## Observações
- Executáveis de `mods/` virtualizados com sucesso e executados a partir de `Data`.
- Nemesis executou e atualizou sem o erro ERROR(1008).
- BodySlide e Pandora operacionais.
- Comportamento de mod desativado validado (executado a partir de `mods/` com ERROR 1008).
- Regressões do SKSE e SSEEdit validadas com sucesso.
