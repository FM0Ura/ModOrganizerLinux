# Fase 5 — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-24-phase5-distribuicao-design.md`](../superpowers/specs/2026-10-24-phase5-distribuicao-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`, com o `install/` do worktree `ModOrganizerLinux-5`. Duas instâncias ou dois jogos configurados.

## 1. Reiniciar no mesmo processo não derruba o MO2 (spec 3.8)

Antes da correção, trocar de instância/jogo e depois fechar o MO2 dava SIGSEGV com `OzoneUtilQt::getQOpenGLContext` na pilha (STATE.md, "Testes manuais de 2026-10-24", item 1). A correção é `Qt::AA_ShareOpenGLContexts` em `main.cpp`.

- [x] Abra uma instância, use **Change Game** (ou abra outra instância pelo gerenciador de instâncias): o MO2 reinicia e mostra a janela principal de novo.
- [x] Feche o MO2 normalmente. Esperado: sai sem queda; `coredumpctl list ModOrganizer` não mostra uma entrada nova.
- [x] Repita a troca duas vezes seguidas antes de fechar. Esperado: igual.

## 2. Atualização pelo MO2 (spec 3.4)

O caminho sem janela (consulta, `SHA256SUMS`, download, troca atômica, reinício na instância aberta só com `--logs`/`--multiple` e `-i <instância>` (sem o `nxm://`, o `moshortcut://` nem o comando da primeira abertura), checksum errado, 404, pasta só de leitura) é coberto por `tests/selfupdate-e2e/run.sh` (servidor local, `MO2_UPDATE_API_URL`, e `MO2_SMOKE_ACCEPT_UPDATE=1`, que aceita a janela sem mostrá-la). Aqui ficam os cliques. **Ambiente diferente da seção 1:** o MO2 roda como AppImage, no host (sem o box), a partir de um AppImage gravado numa pasta sua, por exemplo `~/Applications/ModOrganizer.AppImage`.

Preparação (servidor local, sem GitHub): uma pasta `srv/` com `srv/repos/FM0Ura/ModOrganizerLinux/releases` (a lista de releases no formato da API, um release `v2.5.2.2` com os assets `ModOrganizer-2.5.2.2-x86_64.AppImage` e `SHA256SUMS`, `browser_download_url` em `http://127.0.0.1:8000/download/...`; o `run.sh` gera um modelo), `srv/download/` com o AppImage novo (build com `MO2_VERSION=2.5.2.2`) e o `SHA256SUMS` dele (`sha256sum ModOrganizer-2.5.2.2-x86_64.AppImage > SHA256SUMS`), e `python3 -m http.server --bind 127.0.0.1 --directory srv 8000`. Instalado: um AppImage com `MO2_VERSION=2.5.2.1`. Iniciar com `MO2_UPDATE_API_URL=http://127.0.0.1:8000 ~/Applications/ModOrganizer.AppImage` (variável só de desenvolvimento).

- [x] Com **Check for updates** ligado em Settings, o botão de atualização aparece na barra; clique: a janela "Update" mostra 2.5.2.1 → v2.5.2.2 e as notas do release.
- [x] **Cancel**: nada é baixado; nenhum `.ModOrganizer.AppImage.update-*` em `~/Applications`.
- [x] Clique de novo e aceite: barra de progresso, o MO2 fecha sozinho e abre de novo; **Help → About** mostra 2.5.2.2. `~/Applications/ModOrganizer.AppImage` continua com esse nome e executável (`ls -l`); a mesma instância abriu.
- [x] Com um jogo ou ferramenta rodando pelo MO2 (VFS montado), clique em atualizar. Esperado: aviso "Programs started by Mod Organizer are still running. Close the running programs first…", nada baixado. Feche o programa e repita: a atualização segue.
- [x] Com um download de mod em andamento, aceite a atualização e responda **Cancel** à pergunta "Downloads in progress". Esperado: o MO2 continua aberto (log "update: the exit was cancelled, the new version starts the next time"); depois feche o MO2 normalmente: ele **não** reabre sozinho, e a próxima abertura já é a versão nova.
- [x] Troque o `SHA256SUMS` do servidor por um hash errado e repita (com o AppImage 2.5.2.1 de volta). Esperado: aviso "Update failed … does not match the SHA256SUMS of the release" com link para a página do release; o AppImage antigo intacto, nada em `.update-*`, o MO2 continua aberto.
- [x] Tire o `SHA256SUMS` da lista de assets do release. Esperado: aviso "The release has no SHA256SUMS…" com o link, sem download.
- [x] `chmod a-w ~/Applications` e reinicie o MO2 (com o 2.5.2.1 instalado). Esperado: o botão de atualização aparece mesmo assim (log "update: the folder of … is not writable, the new version can only be downloaded from the page of the release"); clique: aviso "Mod Organizer v2.5.2.2 is available, but the folder of the AppImage (…) is not writable…" com o link para a página do release, que abre no navegador. Nada baixado, nenhum `.update-*`, o AppImage intacto. Volte com `chmod u+w`.
- [x] Sem `MO2_UPDATE_API_URL` e com o repositório privado: sem botão, e o log de depuração diz "no releases found, or the request for them failed" (sem janela de erro).
- [x] Fora do AppImage (`distrobox enter mo2-run -- install/bin/ModOrganizer`): log "not checking for updates, not running from an AppImage".

## 3. Fluxo completo com o AppImage (spec 5.3)

**Ambiente:** Bazzite, usuário limpo, **sem distrobox**: nada de `distrobox enter`, e o `mo2-run` não pode estar em uso. Use a conta (ou um `HOME` novo) sem instância anterior do MO2, para a primeira execução ser de verdade. Os pré-requisitos do host (Steam com Proton, `umu-run`, `fusermount3`) já estão no Bazzite; o jogo tem de ter rodado uma vez pela Steam (prefixo criado).

Os passos 1 a 6 usam o AppImage de um release **rascunho** (ou do artefato do CI `[appimage-ci]`). O passo 7 precisa de **duas versões publicadas** pelo usuário (por exemplo `2.5.2.1` e `2.5.2.2`), com o repositório já público (spec 1.3); a seção 2 acima (servidor local) cobre os cliques da janela sem GitHub, mas não substitui este passo.

1. **Baixar o AppImage.**
   - [x] Baixe `ModOrganizer-<versão>-x86_64.AppImage`, `SHA256SUMS` e o `.debug.tar.zst` do rascunho (ou do artefato) e rode `sha256sum -c SHA256SUMS --ignore-missing`. Esperado: `OK`.
   - [x] O tamanho está perto de 253 MB e o nome tem a versão de quatro números.
2. **Abrir pelo Gear Lever.**
   - [x] No Gear Lever, abra o arquivo, **Unlock** e **Move to the app menu**. Esperado: entrada "Mod Organizer" no menu; abre a janela de escolha de instância, sem terminal.
   - [x] **Help → About** mostra a versão do release.
3. **Criar a instância do Skyrim SE.**
   - [x] Crie a instância (a Steam do Bazzite é detectada). Esperado: a janela principal abre; nenhum aviso de "Python could not be loaded" no ícone de notificações (os plugins Python carregaram); nenhum aviso de dependências do host.
4. **Baixar um mod pelo `nxm://`, inclusive FOMOD.**
   - [x] Entre no Nexus (Settings → Nexus); o login fica após fechar e abrir o MO2 (Secret Service).
   - [x] "Mod Manager Download" no site do Nexus abre o MO2 (pelo `.desktop` do Gear Lever) e baixa; instale o mod. Repita com um mod que tenha FOMOD: o instalador mostra os passos e instala.
5. **Rodar o jogo com SKSE e mods; o jogo não herda variáveis do AppImage.**
   - [x] Com o SKSE e os mods ativos, **Run** pelo MO2. Esperado: o jogo abre e os mods valem (a pasta virtual está montada).
   - [x] Com o jogo rodando, em outro terminal: `pid=$(pgrep -n -f SkyrimSE.exe)` (ou ache o processo do jogo em `pstree -p $(pgrep -x ModOrganizer)`), depois `tr '\0' '\n' < /proc/$pid/environ | grep -E 'APPDIR|APPIMAGE|MO2_APPIMAGE|LD_LIBRARY_PATH|QT_|PYTHON|QTWEBENGINE'`. Esperado: nenhuma variável do AppImage (`APPDIR`, `APPIMAGE`, `MO2_APPIMAGE_ORIG_*`); `LD_LIBRARY_PATH` e `QT_*` só se já existiam no ambiente do host antes de abrir o MO2 (compare com `env` de um terminal comum).
   - [x] O mesmo para um navegador aberto pelo MO2: clique num link que abre o navegador do sistema (por exemplo, o link de um mod nas notas) e leia o `/proc/<pid>/environ` do `firefox`/`chromium` que abriu (`pgrep -n firefox`). Esperado: igual, sem variáveis do AppImage.
   - [x] A janela do jogo recebe o teclado (no KDE Wayland, ver a regra do KWin em `docs/user/pt-BR/troubleshooting.md`, se a janela só piscar na barra).
6. **LOOT, um plugin Python e o BSA Packer.**
   - [x] **Sort** do LOOT ordena os plugins (a masterlist é baixada; sem erro de certificado).
   - [x] Um plugin Python funciona: a pré-visualização de um `.dds` (preview DDS) abre a imagem.
   - [x] **Ferramentas → BSA Packer** gera um `.bsa` do mod de teste na pasta do mod (Skyrim SE, o alvo de validação da 4e); o jogo o carrega com `create_plugins` ligado.
7. **Atualizar para a segunda versão (`2.5.2.N+1`).**
   - [ ] **Pelo MO2:** com o AppImage `2.5.2.N` instalado e o release `2.5.2.N+1` publicado, o botão de atualização aparece, a janela mostra as notas, e ao aceitar o MO2 baixa, fecha e reabre sozinho. **Help → About** mostra `2.5.2.N+1`; o arquivo manteve o nome e o caminho; o `.desktop` do Gear Lever e o `nxm://` continuam funcionando (baixe outro mod pelo `nxm://` depois).
   - [ ] **Variante "trocar de instância antes de atualizar"** (spec 3.8): antes de aceitar a atualização, use **Change Game** (ou abra outra instância pelo gerenciador) uma ou duas vezes, depois atualize. Esperado: sem queda (`coredumpctl list ModOrganizer` sem entrada nova), a atualização segue e o MO2 reabre na instância certa.
   - [ ] **Pelo Gear Lever:** com o AppImage antigo de novo (`2.5.2.N`), o Gear Lever mostra "atualização disponível" e atualiza (baixando só a diferença, via zsync); depois o MO2 abre na versão nova e o `nxm://` funciona.
   - [ ] O arquivo `.debug.tar.zst` do release serve para uma queda de verdade: `coredumpctl gdb ModOrganizer`, com `set debug-file-directory` apontando para a árvore extraída, mostra os nomes de função na pilha (siga `docs/user/pt-BR/troubleshooting.md`, "Relatar uma queda"). Se não houver queda para testar, `kill -SEGV $(pgrep -x ModOrganizer)` num MO2 de teste serve.

Ao fim, o usuário publica os releases (`gh release edit v<versão> --draft=false`) e a Fase 5 fecha.
