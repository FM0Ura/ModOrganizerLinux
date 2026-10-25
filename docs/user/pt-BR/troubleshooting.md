# Problemas conhecidos e solução de problemas

**Português** · [English](../en/troubleshooting.md) · Anterior: [Jogos](games.md)

## Não abre

- **Sem janela no Wayland:** tente `QT_QPA_PLATFORM=xcb ~/Applications/ModOrganizer.AppImage`.
- **Trava logo depois de escolher a instância:** abra com
  `QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu`.
- **"SHM error ... permission denied":** apague os `/tmp/qipc_*_mo*` deixados por uma
  execução anterior como root. Nunca rode o Mod Organizer como root.
- **Por SSH:** exporte antes as variáveis da sessão da área de trabalho (`WAYLAND_DISPLAY`,
  `XDG_SESSION_TYPE`, `XDG_RUNTIME_DIR`, `DISPLAY`, `XAUTHORITY`). Sem tela, o MO2 avisa
  ("needs a graphical session") e sai.
- **O AppImage diz que não consegue rodar:** a glibc precisa ser 2.43 ou mais nova
  ([Instalação](install.md#o-que-você-precisa)), e é preciso o FUSE 3 ou
  `--appimage-extract-and-run`.

## Falta o "umu-run" ou o "fusermount3"

O Mod Organizer confere as [dependências do host](install.md#dependências-do-host) antes de
executar um programa e lista o que falta. Ele abre e gerencia mods sem elas; só o
**Executar** precisa delas.

## O jogo não recebe o teclado (KDE Wayland)

Um jogo iniciado pelo Mod Organizer é uma janela X11 (pelo XWayland) sem id de inicialização,
aberta a partir de uma janela Wayland nativa; a prevenção de roubo de foco do KWin a marca
como "pede atenção" e o teclado fica com a janela anterior. A Steam não tem esse problema.
Contorno, uma regra de janela do KWin:

1. Configurações do Sistema, Gerenciamento de Janelas, Regras de Janela, **Adicionar nova...**
2. **Classe da janela (aplicativo)**, correspondência **Expressão regular**: `steam_app_.*`
3. Adicione a propriedade **Prevenção de roubo de foco**, defina como **Forçar**, **Nenhuma**.
   Aplique.

A correção dentro do Mod Organizer continua em aberto.

## Baldur's Gate 3

- A versão nativa do Linux (`bin/bg3`), que a Steam instala quando o Proton não é forçado,
  não é reconhecida; use o Proton.
- Ainda não há ordem de carga no Linux: o `modsettings.lsx` não é gerado, porque o LSLib
  (`Divine.dll`) rejeita caminhos Unix. O MO2 avisa uma vez por sessão. Gere o arquivo por
  fora e coloque-o no perfil; o MO2 o mapeia para o jogo.
- Com o Proton o jogo abre pelo Mod Organizer, mas um save cujo nome tem acento não foi
  gravado ("Failed to create dir..."). Ainda não foi comparado com o jogo aberto pela Steam,
  sem o Mod Organizer.
- O Script Extender precisa do `WINEDLLOVERRIDES`, em [Jogos](games.md#plugins-python).

## O navegador embutido e as suas autoridades certificadoras

A janela de navegador dentro do Mod Organizer (o login do Nexus, por exemplo) usa os
certificados raiz embutidos no Chromium. Em hosts que não são baseados em Fedora, as
autoridades certificadoras que você adicionou ao repositório do sistema não são confiáveis
ali. Os downloads e a masterlist do LOOT usam o pacote de certificados do host.

## Outras limitações conhecidas

- Se um programa iniciado pelo Mod Organizer travar, a interface do MO2 pode congelar
  enquanto a pasta virtual está montada (o caso do "helper", ainda em investigação). Feche o
  programa, ou use `fusermount3 -u <ponto de montagem>` se ele ficar montado.
- Pandora (janela com duas barras de título, mouse deslocado) e BodySlide (prévia preta)
  têm falhas numa tela 4K com escala 2,7 no KDE; não diagnosticadas.
- Os mods do NieR:Automata sempre passam pelo instalador manual.
- "Remove mod" e "remove download" enviam arquivos para a lixeira só quando a instância está
  no mesmo sistema de arquivos que `~/.local/share/Trash`; em outro disco a operação falha
  com segurança ("failed to delete") e nada é apagado.
- A extração de RAR foi verificada só à mão.
- A verificação do FNIS funciona só para o Skyrim (LE), como no upstream; fica inativa no
  Skyrim SE. O `diagnose_basic` não tem a verificação de atributos NTFS no Linux.
- Reiniciar (Change Game, abrir outra instância) derrubava o MO2 na segunda vez no mesmo
  processo. Está corrigido no AppImage (Fase 5; ainda a confirmar num desktop real); se o
  Mod Organizer ainda cair ao trocar de instância, relate.

## Relatar uma queda

Abra uma [issue](https://github.com/FM0Ura/ModOrganizerLinux/issues/new) com a versão
(Help, About), a distribuição, o que você fez e o backtrace. Para obtê-lo:

1. Baixe o `ModOrganizer-<versão>-x86_64.debug.tar.zst` **do mesmo release** e extraia; é
   uma árvore `.build-id/xx/yyyy.debug`:

   ```sh
   mkdir -p ~/mo2-debug
   tar --zstd -xf ModOrganizer-2.5.2.1-x86_64.debug.tar.zst -C ~/mo2-debug
   ```

2. Abra o último core dump (systemd) no gdb:

   ```sh
   coredumpctl list ModOrganizer
   coredumpctl gdb ModOrganizer
   ```

3. No gdb:

   ```
   (gdb) set debug-file-directory /home/voce/mo2-debug
   (gdb) bt full
   ```

   Se o gdb não achar as bibliotecas do AppImage (o ponto de montagem do AppImage some
   depois da queda), extraia-o com `ModOrganizer.AppImage --appimage-extract` e use
   `set solib-search-path <squashfs-root>/usr/lib` antes do `bt full`.

Anexe também o `logs/crash.log` da instância, se existir.
