# Fase 5 — Distribuição (AppImage, atualização, release, documentação) — Design

- **Data:** 2026-10-24
- **Status:** em revisão
- **Spec geral:** [`2026-10-14-mo2-linux-port-design.md`](2026-10-14-mo2-linux-port-design.md) (§3.4 "Fase 5 — Distribuição"; este documento a substitui onde divergirem)
- **Branch:** `fase-5-distribuicao` (a partir de `fase-4e-bsa` em `398dc5f0`, no worktree `../ModOrganizerLinux-5`; o PR mira o `main` depois que os PRs #3 a #9 entrarem nele, ou `fase-4e-bsa` até lá)

## 1. Objetivo

Hoje o MO2 do Linux só roda dentro do distrobox `mo2-run` (Fedora 44), porque o binário usa o Qt 6.11, o Python 3.14 e o PyQt6 do sistema, e o `mo2-vfs` chega ao host pela ponte `distrobox-host-exec`. Quem não montou o ambiente de desenvolvimento não tem como usar.

A Fase 5 entrega o MO2 como **um AppImage** que roda direto no host: baixar, abrir (pelo Gear Lever no Bazzite ou executando o arquivo) e usar. O MO2 se atualiza sozinho a partir dos releases do GitHub. Cada release é gerado pelo CI a partir de uma tag. Há documentação de usuário em inglês e em português.

**Por que agora:** as Fases 1 a 4 fecharam as funcionalidades. O que separa o port de "instalação em um clique" (critério de sucesso 1.1 da spec geral, item 1) é o empacotamento.

**Público (decisão do usuário, 2026-10-24):** não só o Bazzite do usuário; também outras distros, depois que o repositório ficar público.

### 1.1 Decisões do usuário (2026-10-24)

| Tema | Decisão |
|---|---|
| Formato | AppImage (descartados: tarball com script de instalação, distrobox, Flatpak, RPM por `rpm-ostree`) |
| Atualização | **b + c:** (b) informação de atualização embutida no AppImage (`gh-releases-zsync`), usada pelo Gear Lever e pelo AppImageUpdate, com download só da diferença (zsync); (c) o verificador de versões do próprio MO2 (`SelfUpdater`) ligado no Linux, apontando para os releases do nosso repositório, baixando o AppImage novo, trocando o arquivo e reiniciando |
| Distros | Build continua no Fedora 44 (sem base antiga). Declarar a glibc mínima e uma lista de distros testadas. Base antiga (Ubuntu 22.04/24.04, AlmaLinux) fica para quando alguém pedir |
| Teste em outras distros | Containers bastam (até a janela principal); o jogo real só no Bazzite |
| Documentação | Nas duas línguas: inglês e português |
| Visibilidade do repositório | Público nesta fase, para os releases e a atualização funcionarem; a decisão sobre o Nexus fica para depois (§1.3) |
| Queda ao reiniciar | Corrigida nesta fase (§3.8): o reinício da atualização passaria por ela |

### 1.2 Decisões de quem escreveu esta spec (o usuário pode revisar)

| Tema | Decisão |
|---|---|
| `umu-run` e FUSE | Não embutidos. Ficam como dependências do host; o MO2 verifica e avisa com instruções (§3.6) |
| Símbolos de depuração | Separados do binário; publicados no release como arquivo à parte (§3.5) |
| Plataformas do release | Só Linux x86_64. O CI do Windows continua só como verificação de build (`[windows-ci]`) |
| Versão | A do upstream com um 4º número do port: `2.5.2.N` (tag `v2.5.2.N`, N ≥ 1). O `Version::parse(..., ParseMode::MO2)` (`uibase/src/versioning.cpp`) rejeita sufixos como `-linux.N` (só aceita dev/a/alpha/b/beta/rc, um 4º número opcional ou `+metadados`), e o 4º número ordena `2.5.2 < 2.5.2.1 < 2.5.2.2 < 2.5.3.1` (§3.4) |
| Disparo do release | Tag `v*` (e `workflow_dispatch`); cria um release **rascunho**, nunca publicado sozinho (§3.5) |
| Documentação | `README.md` em inglês (porta de entrada), `README.pt-BR.md`, guia em `docs/user/en/` e `docs/user/pt-BR/`; ROADMAP, STATE, specs e planos continuam só em português |
| Ambiente dos filhos | Limpeza do processo inteiro no início do `main`, não por `spawn` (§3.3) |
| Assinatura | Sem assinatura GPG nesta fase: HTTPS do GitHub + `SHA256SUMS` no release; o `SelfUpdater` confere o SHA-256 antes de trocar o arquivo |

### 1.3 Repositório público (decisão do usuário, 2026-10-24)

O usuário decidiu tornar público o próprio `FM0Ura/ModOrganizerLinux` nesta fase: é o que permite testar de ponta a ponta as atualizações (b) e (c), que só enxergam releases publicados de um repositório público (o `SelfUpdater` ignora rascunhos, e a API sem autenticação não vê repositórios privados).

- **Antes de mudar a visibilidade** (passo do plano, com confirmação do usuário no momento): o commit GPLv3 `3269fa8b` (branch `licenca-gpl`) entra; varredura do histórico por segredos e dados pessoais (tokens, logs, caminhos com dados da conta), com o resultado mostrado ao usuário.
- **Nexus:** continua pendente (decisão adiada pelo usuário). Os releases públicos saem com o client id `modorganizer2` (`nexusoauthconfig.cpp`, sobreponível por `MO2_NEXUS_CLIENT_ID`); o usuário aceitou isso por ora. O registro de app próprio e o rebrand ficam fora desta fase.
- O CI continua criando o release como **rascunho**; publicar é um passo manual do usuário.

## 2. Escopo

### 2.1 Metas

1. **AppImage x86_64** com tudo o que hoje vem do container:
   - `ModOrganizer`, `libuibase`, `libmo2-archive`, todos os plugins C++ e Python, recursos, estilos, traduções, tutoriais;
   - `mo2-vfs`, `7z.so`, `lootcli` + `libloot`;
   - Qt 6.11 com QtWebEngine (`QtWebEngineProcess`, recursos e locales), plataformas `xcb` e `wayland`, plugins de imagem/ícones usados;
   - Python 3.14 (interpretador embutido, biblioteca padrão) e PyQt6, contra o **mesmo** Qt embutido;
   - licenças e avisos de terceiros (§3.7).
2. **Rodar direto no host**, sem distrobox: o `mo2-vfs` e os jogos rodam no mesmo namespace da Steam; a ponte `MO2_VFS_BRIDGE=auto` detecta que não está num container e usa `direct`.
3. **Ambiente limpo para os programas iniciados:** jogos, ferramentas, `umu-run` e qualquer processo filho que não seja do próprio AppImage recebem o ambiente original do host, sem as variáveis do AppImage (§3.3).
4. **Integração com o desktop:** funciona com o Gear Lever (Bazzite) e sem ele; o handler `nxm://` aponta para o `$APPIMAGE` (o `nxmhandler_linux.cpp` já lê `APPIMAGE`) e se corrige quando o arquivo muda de lugar ou de nome numa atualização.
5. **Atualização (b):** `.zsync` no release e a informação `gh-releases-zsync|FM0Ura|ModOrganizerLinux|latest|ModOrganizer-*-x86_64.AppImage.zsync` embutida.
6. **Atualização (c):** `SelfUpdater` ligado no Linux quando o MO2 roda como AppImage (§3.4).
7. **CI de release:** workflow que gera AppImage, `.zsync`, símbolos de depuração e `SHA256SUMS` e cria o release rascunho (§3.5).
8. **Teste em outras distros:** smoke test do AppImage, até a janela principal, em containers limpos (§5.2).
9. **Verificação das dependências do host:** `umu-run` e `fusermount3`, com mensagens claras (§3.6).
10. **Documentação de usuário** nas duas línguas (§4).
11. **Queda ao reiniciar o MO2 no mesmo processo** (STATE, testes de 2026-10-24, item 1), §3.8.
12. **Pendências da Fase 5 já registradas no `STATE.md`:** nota de proveniência/licença do `dxgiformat.h`; `7z.so` com libstdc++ dinâmica; símbolos de depuração separados (binário de ~345 MB); cache da imagem do container no CI; avisos do Node 20 nas actions.

### 2.2 Não-metas

- Distros com glibc ou libstdc++ mais antigas que as do Fedora 44 (sem base de build antiga).
- Flatpak, RPM, pacote do AUR, Steam Deck / modo Gaming.
- Release para Windows.
- Embutir `umu-run`, Proton ou Steam.
- Registrar o app no Nexus, rebrand (§1.3).
- As demais correções dos testes manuais de 2026-10-24 (ROADMAP, "Depois da Fase 5"); só a queda ao reiniciar entra (§3.8). O `execv` fica restrito ao reinício da atualização (§3.4).
- Assinatura criptográfica dos releases.
- aarch64.

### 2.3 Antimetas (falha mesmo funcionando)

- **Vazamento de ambiente:** um jogo iniciado pelo MO2 herdar `LD_LIBRARY_PATH`, `QT_*`, `PYTHONHOME`/`PYTHONPATH`, `APPDIR` etc. do AppImage e quebrar (Proton/Wine/Steam runtime são sensíveis a isso).
- **Atualização que estraga a instalação:** arquivo pela metade, permissão de execução perdida, ou o AppImage antigo apagado antes de o novo estar verificado. A troca é atômica; em qualquer falha, o antigo continua intacto.
- **Release publicado sem querer:** o CI só cria rascunhos; publicar é decisão manual do usuário.
- **Quebrar o fluxo de desenvolvimento:** `./build.sh`, o distrobox `mo2-run` e o CI atual continuam funcionando como hoje; o AppImage é um passo a mais, não uma substituição.
- **Gastar minutos do Actions:** o build de release roda só por tag ou à mão, não a cada push; os smoke tests de distros também (ou em `[appimage-ci]`, análogo ao `[windows-ci]`).
- **Ler o Python ou o PyQt6 do host:** o AppImage nunca carrega `site-packages`, stdlib ou Qt de fora (mesmo que o host tenha Python 3.14).

## 3. Design

### 3.1 Montagem do AppImage

- Ferramentas: `linuxdeploy` + `linuxdeploy-plugin-qt` (decisão da spec geral) e `appimagetool`/runtime **type 2 estático** (não depende de `libfuse2` no host). Versões fixadas e baixadas por um script `build/appimage/build-appimage.sh`, no estilo dos `build/*/build-*.sh` existentes; roda no container `mo2-linux-build`.
- Entrada: a árvore `install/` do `./build.sh` (hoje `bin/`, `lib/`, `share/`; RPATH `$ORIGIN/../lib`). O AppDir reproduz esse layout sob `usr/` para não mudar como o MO2 acha plugins e recursos (`applicationDirPath()`).
- `.desktop` e ícone do MO2 no AppDir (com `MimeType=x-scheme-handler/nxm;` para o Gear Lever registrar o handler).
- O `QtWebEngineProcess` precisa achar os recursos e locales embutidos; o plugin Qt do `linuxdeploy` cuida disso, mas o smoke test (§5.2) deve abrir algo que use o WebEngine ou ao menos verificar que o processo inicia.
- **libstdc++/libgcc_s:** o build usa o GCC do Fedora 44; distros com GCC mais antigo não têm os símbolos `GLIBCXX_*` exigidos. O AppImage embute `libstdc++.so.6` e `libgcc_s.so.1` e usa os do host só se forem mais novos (técnica `checkrt`/"usar o mais novo"); o `7z.so`, que hoje linka a libstdc++ dinamicamente (pendência do STATE), passa pelo mesmo mecanismo. Escolha da técnica: do builder.
- **glibc mínima:** a do Fedora 44, registrada no README e conferida por script (maior versão `GLIBC_*` exigida por todos os ELF do AppDir).
- Nome: `ModOrganizer-<versão>-x86_64.AppImage`.

### 3.2 Python embutido

- Interpretador: o `libpython3.14` do container, a biblioteca padrão (`lib/python3.14`, sem `test/`, `idlelib`, `tkinter`, `__pycache__` desnecessários) e os módulos de extensão necessários; PyQt6 (`site-packages/PyQt6`) do Fedora, com seus `.so` resolvendo o Qt **embutido** (mesma versão 6.11; ajustar RPATH se preciso).
- O `pythonrunner.cpp` (Linux) hoje deixa o Python calcular os caminhos do sistema e filtra o `sys.path` por `systemSysPathRoots()` (raiz `sys.base_prefix`). No AppImage, o `PyConfig.home` (ou equivalente) aponta para o Python do AppDir, de forma que `sys.base_prefix` seja o do AppDir e o filtro continue valendo sem mudança de regra. Fora do AppImage (build de desenvolvimento), o comportamento atual não muda.
- O diagnóstico "Python could not be loaded" (STATE) continua existindo, mas no AppImage não deve aparecer nunca.

### 3.3 Ambiente dos processos filhos

Decisão desta spec (revisão do builder): a limpeza vale para o **processo inteiro**, não por chamada de `spawn`, porque muitos filhos não passam pelo código de `spawn` do MO2 (`QDesktopServices::openUrl` → `xdg-open` do próprio Qt, `subprocess` dos plugins Python como o `basic_games`, `QProcess` em plugins).

- O `AppRun` define o mínimo possível (o AppDir resolve bibliotecas por RPATH; nada de `LD_LIBRARY_PATH` global se der para evitar) e guarda o valor original de cada variável que alterar, por exemplo em `MO2_APPIMAGE_ORIG_<VAR>` (com marca para "não existia").
- Logo no início do `main` (antes de criar o `QApplication` e de iniciar o Python), o MO2 lê o que precisa (caminho do AppDir, do Python, do `QtWebEngineProcess`, dos plugins Qt), configura isso por API (`QCoreApplication::addLibraryPath`, `PyConfig.home`, `QTWEBENGINEPROCESS_PATH` só para o processo do WebEngine, etc.) e **restaura no próprio processo o ambiente original do host**. Assim qualquer filho, de qualquer caminho, herda o ambiente do host.
- Programas do próprio AppImage que precisam de variáveis (`QtWebEngineProcess`, `lootcli`, `mo2-vfs`) recebem-nas explicitamente, no ponto em que são iniciados; o builder confirma, para cada um, se o RPATH basta.
- O reinício após a atualização (§3.4) parte do ambiente do host, e o `AppRun` do AppImage novo refaz o resto.
- Testes: a restauração (o que existia volta ao valor original, o que não existia some) e, no teste manual, o `/proc/<pid>/environ` do jogo e de um navegador aberto pelo MO2.

### 3.4 Atualização dentro do MO2 (c)

- `SelfUpdater::testForUpdate` hoje retorna logo no Linux ("there are no Linux releases yet"). Passa a:
  - checar só quando `APPIMAGE` está definido e aponta para um arquivo gravável (instalação pelo AppImage); fora disso, continua sem checar (log de depuração);
  - respeitar `offlineMode`, `checkForUpdates` e `usePrereleases` como no Windows;
  - consultar `GitHub::Repository("FM0Ura", "ModOrganizerLinux")` no Linux (Windows mantém `Modorganizer2/modorganizer`); com o repositório privado a API responde 404, que deve ser tratado como "sem atualização" e só logado.
- Escolha do asset: no Linux, pelo nome (`ModOrganizer-*-x86_64.AppImage`), não pelo `content_type` `application/x-msdownload` do Windows.
- Download para um arquivo temporário **no mesmo diretório** do `$APPIMAGE` (para o `rename` ser atômico), conferência do SHA-256 contra o `SHA256SUMS` do release, `chmod` com as permissões do antigo, `rename` por cima do `$APPIMAGE`. Se o diretório não for gravável, o MO2 avisa e oferece abrir a página do release (sem trocar nada).
- Nome do arquivo: se o `$APPIMAGE` tem a versão no nome (Gear Lever costuma renomear), a troca mantém o **caminho atual** do arquivo para não quebrar o `.desktop` e o handler `nxm://`; se o handler `nxm://` aponta para outro caminho, o MO2 o reescreve como já faz (meta 4).
- Reinício: `execv` do `$APPIMAGE` com os mesmos argumentos e o ambiente original (§3.3), depois de fechar o MO2 do jeito normal (o mesmo caminho de saída de hoje, com o VFS desmontado). Não reiniciar com mount ativo: se há programa rodando pelo VFS, a atualização espera ou pede para fechar.
- **Versões:** `MO2_VERSION_STRING`/`MO2_VERSION_PRERELEASE` (hoje do `version.rc`, `2.5.2`) passam a poder vir do CI (tag `v2.5.2.N` → `2.5.2.N`); builds locais continuam `2.5.2` (desenvolvimento). Testes unitários da ordenação `2.5.2 < 2.5.2.1 < 2.5.2.2 < 2.5.3.1`.
- A janela de "nova versão" do upstream (`UpdateDialog`, com as notas do release) é reaproveitada.

### 3.5 CI de release e símbolos

- Novo workflow `release-linux.yml`: `on: push: tags: ['v*']` e `workflow_dispatch`. Passos: imagem do container (com cache — pendência do STATE), `./build.sh` com testes, separação de símbolos, `build-appimage.sh`, `.zsync`, `SHA256SUMS`, smoke tests (§5.2), e `gh release create --draft` com os arquivos. **Nunca** publicado automaticamente (§1.3, §2.3).
- Símbolos: para cada ELF do AppDir, `objcopy --only-keep-debug` → `.debug`, `strip --strip-debug` (ou `--strip-unneeded` onde seguro) e `--add-gnu-debuglink`; um `ModOrganizer-<versão>-x86_64.debug.tar.zst` com a árvore dos `.debug` e o `build-id` de cada um, suficiente para o `gdb`/`coredumpctl` de quem relatar uma queda.
- O `linux.yml` atual continua como está (build + testes a cada push); a correção dos avisos do Node 20 (atualizar as actions) vale para todos os workflows.

### 3.6 Dependências do host

- Na abertura (ou na primeira execução de um programa), o MO2 confere `fusermount3` e `umu-run` no `PATH` do host (ambiente original). Faltando, a mensagem diz o que falta, para que serve e como instalar (link para a documentação de usuário, §4), em vez dos erros de hoje no `spawn`.
- O MO2 abre e gerencia mods sem eles; só "Executar" depende deles.

### 3.7 Licenças

- O AppImage leva `usr/share/licenses/` (ou `usr/share/doc/ModOrganizer/`) com: a licença do port (GPLv3, quando o commit da `licenca-gpl` entrar; até lá, a do upstream MO2), Qt (LGPLv3), QtWebEngine/Chromium (avisos de terceiros), Python (PSF), PyQt6 (GPLv3), 7-Zip (LGPL + restrição do unRAR), libloot (GPLv3), libbsarchpp (GPLv3), boost, fmt, spdlog, lz4, zlib, gsl-lite, tbb, toml++, e a nota de proveniência/licença do `dxgiformat.h` (pendência do STATE).

### 3.8 Queda ao reiniciar no mesmo processo

- Causa (STATE, testes de 2026-10-24, item 1): no Linux o MO2 reinicia no laço do `main.cpp` com o mesmo `QApplication`; cada `qApp->exit()` emite `aboutToQuit`, e o QtWebEngine (`OzoneUtilQt::getQOpenGLContext()`) apaga duas vezes o `QOpenGLContext` de reserva que liga a esse sinal.
- Correção: ligar `Qt::AA_ShareOpenGLContexts` antes de criar o `QApplication` (com o contexto global compartilhado, o QtWebEngine não cria o de reserva). Se não bastar, o builder volta ao usuário antes de adotar outra correção (como reiniciar todos os caminhos por `execv`).
- Teste: o cenário dos core dumps (trocar de jogo / abrir instância pelo gerenciador / criar instância e depois reiniciar ou fechar de novo) automatizado no smoke test se possível, e no checklist manual; o passo 7 do teste manual inclui a variante "trocar de instância antes de atualizar".
- A queda em `TutorialControl::lockUI` (item 2) continua fora.

## 4. Documentação de usuário

- `README.md` em inglês, `README.pt-BR.md` em português, com links cruzados. O conteúdo atual do README que é de desenvolvedor (build, distrobox, CI) vai para uma seção "Development"/"Desenvolvimento" ou para `docs/dev/`, sem perder nada.
- Guia em `docs/user/en/` e `docs/user/pt-BR/`, mesmo conteúdo:
  1. instalação: baixar, Gear Lever (Bazzite), executar direto, distros testadas, glibc mínima, dependências do host (`umu-run`, FUSE 3, Steam com Proton) e como instalá-las nas distros testadas;
  2. primeiros passos: criar instância, Steam/Proton, `nxm://`, login do Nexus (keyring);
  3. atualização: pelo MO2 e pelo Gear Lever/AppImageUpdate;
  4. jogos que funcionam (tabela do README: Skyrim SE, Cyberpunk 2077, NieR:Automata) e ferramentas testadas;
  5. problemas conhecidos (do STATE: foco no KDE Wayland e a regra do KWin, reinício ao trocar de instância, BG3, etc.) e como relatar uma queda (`coredumpctl`, símbolos do release).
- `docs/testing/phase5-manual-checklist.md` em português, como nas fases anteriores.

## 5. Testes

### 5.1 Automáticos (no `./build.sh --tests` ou em testes do próprio AppImage)

- Ordenação das versões `2.5.2.N` (§3.4).
- Escolha do asset e verificação do SHA-256 do `SelfUpdater`, contra respostas JSON gravadas (sem rede).
- Troca atômica: diretório não gravável, falha no meio do download, hash errado — o arquivo antigo fica intacto.
- Restauração do ambiente do host no início do `main` (§3.3): o que existia volta ao original, o que não existia some.
- Script que confere a glibc máxima exigida pelos ELF do AppDir e falha se passar da declarada.
- Nenhum ELF do AppDir resolve uma biblioteca fora do AppDir além da lista permitida (glibc, libGL/EGL/Vulkan, X11/Wayland, fontconfig/freetype, libdbus/libsecret conforme o `excludelist`), conferido com `ldd` num container limpo.

### 5.2 Smoke tests em containers limpos

- `fedora:44` sem os pacotes de desenvolvimento, `archlinux:latest` e o Ubuntu mais recente cuja glibc seja ≥ a do Fedora 44 (se nenhum Ubuntu atender, registrar e ficar com Fedora + Arch).
- Em cada um: executar o AppImage com `--appimage-extract-and-run` (sem FUSE no container), `QT_QPA_PLATFORM=offscreen`, até a janela principal, como o smoke test atual (`tests/smoke`, `MO2_SMOKE_STRICT_HOME`); confirmar que os plugins Python carregaram (o painel de diagnóstico sem "Python could not be loaded") e que o `lootcli --help` e o `mo2-vfs` iniciam.
- Rodam no workflow de release e sob demanda (`[appimage-ci]` ou `workflow_dispatch`), não a cada push.

### 5.3 Teste manual (Bazzite, usuário limpo, sem distrobox)

1. Baixar o AppImage do release rascunho (ou do artefato do CI).
2. Abrir pelo Gear Lever.
3. Criar instância do Skyrim SE.
4. Baixar um mod pelo `nxm://` (inclusive FOMOD).
5. Rodar o jogo com SKSE e mods; o jogo não herda variáveis do AppImage (conferir `/proc/<pid>/environ`).
6. LOOT, um plugin Python (ex.: preview DDS) e o BSA Packer funcionam.
7. Publicar uma segunda versão (`2.5.2.N+1`) e ver o MO2 avisar, atualizar, reiniciar sozinho, e o `nxm://` continuar funcionando; repetir pela atualização do Gear Lever.

O passo 7 roda com o repositório já público (§1.3), com duas versões publicadas pelo usuário (ex.: `2.5.2.1` e `2.5.2.2`), e inclui a variante "trocar de instância antes de atualizar" (§3.8).

## 6. O que fica com o builder

- Versões exatas de `linuxdeploy`, plugin Qt, runtime e `appimagetool`; a técnica de "libstdc++ mais nova"; o que exatamente podar do Python e do QtWebEngine.
- O formato exato das variáveis `MO2_APPIMAGE_ORIG_*` e o que cada programa do AppImage precisa receber explicitamente.
- A organização dos scripts e workflows, desde que siga o padrão de `build/*/build-*.sh` e do `build.sh`.
- A distro Ubuntu do smoke test, conforme a glibc.

## 7. Riscos

| Risco | Mitigação |
|---|---|
| Tamanho do AppImage (QtWebEngine + Python) acima de 500 MB | Podar stdlib e locales; aceitar; registrar o tamanho no ROADMAP |
| zsync pouco eficiente sobre squashfs comprimido | Aceito: (c) baixa o arquivo inteiro de qualquer forma |
| PyQt6 do Fedora com RPATH para `/usr/lib64` | Ajustar RPATH no AppDir; teste do `ldd` (§5.1) |
| Ubuntu recente com glibc mais antiga que a do Fedora 44 | Registrar e documentar como não suportada (decisão 1 do usuário) |
| GitHub API sem autenticação limitada a 60 req/h | Uma consulta por abertura do MO2 (como o upstream) |
