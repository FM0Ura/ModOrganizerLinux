# Fase 4c.2 — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-20-phase4c2-pendencias-design.md`](../superpowers/specs/2026-10-20-phase4c2-pendencias-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`. Instância de Skyrim SE das fases anteriores (com SKSE e alguns mods).

**Build:** o `install/` do worktree `ModOrganizerLinux-4c2` (`./build.sh` na distrobox `mo2-run`) ou, depois de um run Linux verde do PR, o artefato `mo2-linux-install` desse run (restaure a permissão de execução: `chmod +x <pasta do build>/bin/ModOrganizer <pasta do build>/bin/mo2-vfs`). O `mo2-vfs` precisa ficar na mesma pasta que o `ModOrganizer`.

## Preparação

- O item 1 roda por SSH, **sem exportar** `WAYLAND_DISPLAY`, `DISPLAY` nem as demais variáveis da sessão gráfica.
- O item 2 pede o Skyrim SE; a parte de gravação é opcional e pede um jogo com invalidação de arquivos (Fallout 4, Skyrim LE, Oblivion ou Fallout 3/New Vegas).
- O item 3 usa o SKSE (ou o **Shell VFS** da Fase 2), um segundo executável (ou o LOOT) e dois terminais: um no host, para o `pkill` e o `findmnt`, e o MO2 aberto pela área de trabalho. Anote o log do MO2 (`logs/mo_interface.log` da instância) antes de começar.

## 1. Sem tela

- [ ] Por SSH (sem exportar as variáveis da sessão gráfica):
  `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer; echo "saída $?"`.
  Esperado: a mensagem `Mod Organizer needs a graphical session, ...` e `saída 1`; nenhum
  "has crashed" nem backtrace.

## 2. Arquivos (BSA/BA2) no INI certo

O Skyrim SE não tem invalidação de arquivos no MO2 (o plugin do jogo não registra
**Automatic Archive Invalidation**), e marcar ou desmarcar um BSA na aba **Archives** não
grava o INI. Com ele, o item só confere a leitura; a gravação no INI com outra caixa é
coberta pelo `test_profile_inis_linux.cpp` (`deps/game_bethesda/tests`).

- [ ] Com o Skyrim SE, no perfil com **INIs por perfil desligados** (**Profiles...** ->
  "Use profile-specific Game INI Files" desmarcado): abrir o perfil, olhar a aba
  **Archives** e executar o jogo uma vez pelo MO2.
  Esperado: no prefixo (`.../steamapps/compatdata/489830/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/`)
  **não** aparece um `skyrim.ini` minúsculo ao lado do `Skyrim.ini`: `ls` nessa pasta.
- [ ] (Opcional) Gravação, com um jogo que tenha invalidação e esteja instalado: Fallout 4
  (o mais simples), Skyrim LE, Oblivion ou Fallout 3/New Vegas. Com o Fallout 4, em
  **Profiles...**, ligar e desligar **Automatic Archive Invalidation**.
  Esperado, com os INIs por perfil desligados: no prefixo
  (`.../compatdata/377160/pfx/drive_c/users/steamuser/Documents/My Games/Fallout4/`) o
  `Fallout4Custom.ini` muda (`bInvalidateOlderFiles` em `[Archive]`) e não aparece um
  `fallout4custom.ini` ao lado. Com os INIs por perfil ligados ("Use profile-specific Game
  INI Files"): o `fallout4custom.ini` do perfil (`<instância>/profiles/<perfil>/`) muda e
  não aparece um `Fallout4Custom.ini` nessa pasta.

## 3. Helper travado

Com um programa aberto pelo MO2, um F5 não pede nada ao VFS (o refresh só fica adiado
enquanto o VFS está ativo), e ativar ou instalar um mod também não. Só um novo
**Run** (ou o **Sort** do LOOT) pede um `update` ao helper; por isso o item destrava a
interface com **Unlock** e executa um segundo programa.

- [ ] Executar o SKSE (ou o **Shell VFS** da Fase 2) pelo MO2. Com ele aberto, num terminal
  do host: `pkill -STOP -x mo2-vfs`.
- [ ] No MO2, clicar em **Unlock** no aviso "Mod Organizer is locked while the application
  is running." e executar (**Run**) um segundo programa (ou **Sort** do LOOT).
  Esperado: a janela do MO2 para de responder por cerca de 30 s (o `update` é síncrono e
  expira); depois o log mostra `cannot update the virtual file system: mo2-vfs did not
  reply to update in time` e `mo2-vfs stopped answering while programs use the virtual
  file system`, e o MO2 mostra o erro (**Error**, ou "failed to start loot" no LOOT)
  `the virtual file system of an earlier mount is still mounted on ... and cannot be
  unmounted; close the programs that use it and run fusermount3 -uz on it`. O segundo
  programa não abre. Tentar de novo dá o mesmo erro "still mounted", já sem a espera.
- [ ] Apertar F5.
  Esperado: a lista de plugins **não** fica vazia (o refresh fica adiado).
- [ ] No host, `pkill -CONT -x mo2-vfs` e fechar o programa.
  Esperado: o helper volta a responder e desmonta sozinho: o mount some
  (`findmnt -n -o TARGET,FSTYPE | grep mo2-vfs` vazio no host) e o refresh adiado roda
  (lista de plugins completa). Neste caminho o MO2 não registra a linha
  `the virtual file system is no longer mounted on ...`.
- [ ] (Opcional) Variante com o helper morto: repetir os três primeiros passos e, em vez
  de `pkill -CONT`, rodar no host `pkill -KILL -x mo2-vfs` e fechar o programa.
  Esperado: em até ~5 s o log mostra `the virtual file system is no longer mounted on ...`
  (o MO2 desmonta o mount morto), mais uma linha de erro `mo2-vfs ended (...) while the
  virtual file system was active`, e o refresh adiado roda (lista de plugins completa).
- [ ] Fechar e abrir o MO2.
  Esperado: sem aviso de mount órfão.

## 4. Regressão rápida

- [ ] Skyrim SE + SKSE com mods pelo "Executar" (**Run**); o LOOT ordena; fechar o jogo
  desmonta (`findmnt -n -o TARGET,FSTYPE,SOURCE | grep mo2-vfs` vazio).

**Ao terminar:** registre aqui o resultado de cada item e, em "Observações", o que precisou de correção.

## O que enviar se falhar

1. **Qual item** falhou e o que você viu (print ajuda).
2. **Saída do terminal** do MO2 (e, no item 1, a saída completa do comando).
3. **Logs da instância**: `logs/` (`mo_interface.log`, `mo2-vfs.log` e, se não estiver vazio, `crash.log`).
4. O estado do mount, no host: `findmnt -n -o TARGET,FSTYPE,SOURCE | grep mo2-vfs`.
5. A **versão do build** (commit).

## Observações
