# Fase 4c.2 — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-20-phase4c2-pendencias-design.md`](../superpowers/specs/2026-10-20-phase4c2-pendencias-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`. Instância de Skyrim SE das fases anteriores (com SKSE e alguns mods).

**Build:** o `install/` do worktree `ModOrganizerLinux-4c2` (`./build.sh` na distrobox `mo2-run`) ou, depois de um run Linux verde do PR, o artefato `mo2-linux-install` desse run (restaure a permissão de execução: `chmod +x <pasta do build>/bin/ModOrganizer <pasta do build>/bin/mo2-vfs`). O `mo2-vfs` precisa ficar na mesma pasta que o `ModOrganizer`.

## Preparação

- O item 1 roda por SSH, **sem exportar** `WAYLAND_DISPLAY`, `DISPLAY` nem as demais variáveis da sessão gráfica.
- O item 2 pede o Skyrim SE com um mod que tenha um `.bsa`; o Fallout 4 é opcional.
- O item 3 usa o SKSE (ou o **Shell VFS** da Fase 2) e dois terminais: um no host, para o `pkill` e o `findmnt`, e o MO2 aberto pela área de trabalho. Anote o log do MO2 (`logs/mo_interface.log` da instância) antes de começar.

## 1. Sem tela

- [ ] Por SSH (sem exportar as variáveis da sessão gráfica):
  `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer; echo "saída $?"`.
  Esperado: a mensagem `Mod Organizer needs a graphical session, ...` e `saída 1`; nenhum
  "has crashed" nem backtrace.

## 2. Arquivos (BSA/BA2) no INI certo

- [ ] Com o Skyrim SE, no perfil com **INIs por perfil desligados** (**Profiles...** ->
  "Use profile-specific Game INI Files" desmarcado), aba **Archives**: desmarcar e marcar
  de novo um BSA de mod, com a opção de gerenciar arquivos ligada (ou ligar e desligar
  **Automatic Archive Invalidation** em **Profiles...**).
  Esperado: no prefixo (`.../steamapps/compatdata/489830/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/`), o
  `Skyrim.ini` (o arquivo real, com maiúscula) muda
  (`SResourceArchiveList`/`bInvalidateOlderFiles`), e **não** aparece um `skyrim.ini`
  minúsculo ao lado: `ls` nessa pasta.
- [ ] (Opcional) Com o Fallout 4 instalado, o mesmo, conferindo `Fallout4Custom.ini`. Com
  os INIs por perfil ligados, o `fallout4custom.ini` do perfil
  (`<instância>/profiles/<perfil>/`) muda e não aparece um `Fallout4Custom.ini` nessa pasta.

## 3. Helper travado

- [ ] Executar o SKSE (ou o **Shell VFS** da Fase 2) pelo MO2. Com ele aberto, num terminal
  do host: `pkill -STOP -x mo2-vfs`.
- [ ] No MO2, provocar um refresh (F5, ou instalar/ativar um mod), que pede um `update` ao
  VFS. A janela do MO2 pode parar de responder por cerca de 30 s enquanto o pedido expira
  (o pedido é síncrono).
  Esperado: depois de ~30 s o MO2 registra no log
  `mo2-vfs stopped answering while programs use the virtual file system` e **não** mostra
  a lista de plugins vazia (o refresh fica adiado).
- [ ] No host, `pkill -CONT -x mo2-vfs` e fechar o programa.
  Esperado: o helper volta a responder e desmonta sozinho: o mount some
  (`findmnt -n -o TARGET,FSTYPE | grep mo2-vfs` vazio no host) e o refresh adiado roda
  (lista de plugins completa). Neste caminho o MO2 não registra a linha
  `the virtual file system is no longer mounted on ...`.
- [ ] (Opcional) Variante com o helper morto: repetir os dois primeiros passos e, em vez
  de `pkill -CONT`, rodar no host `pkill -KILL -x mo2-vfs` e fechar o programa.
  Esperado: em até ~5 s o log mostra `the virtual file system is no longer mounted on ...`
  (o MO2 desmonta o mount morto) e o refresh adiado roda (lista de plugins completa).
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
