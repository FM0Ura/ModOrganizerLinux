# Fase 5 — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-24-phase5-distribuicao-design.md`](../superpowers/specs/2026-10-24-phase5-distribuicao-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`, com o `install/` do worktree `ModOrganizerLinux-5`. Duas instâncias ou dois jogos configurados.

## 1. Reiniciar no mesmo processo não derruba o MO2 (spec 3.8)

Antes da correção, trocar de instância/jogo e depois fechar o MO2 dava SIGSEGV com `OzoneUtilQt::getQOpenGLContext` na pilha (STATE.md, "Testes manuais de 2026-10-24", item 1). A correção é `Qt::AA_ShareOpenGLContexts` em `main.cpp`.

- [ ] Abra uma instância, use **Change Game** (ou abra outra instância pelo gerenciador de instâncias): o MO2 reinicia e mostra a janela principal de novo.
- [ ] Feche o MO2 normalmente. Esperado: sai sem queda; `coredumpctl list ModOrganizer` não mostra uma entrada nova.
- [ ] Repita a troca duas vezes seguidas antes de fechar. Esperado: igual.

## 2. Atualização pelo MO2 (spec 3.4)

O caminho sem janela (consulta, `SHA256SUMS`, download, troca atômica, reinício com os mesmos argumentos, checksum errado, 404) é coberto por `tests/selfupdate-e2e/run.sh` (servidor local, `MO2_UPDATE_API_URL`, e `MO2_SMOKE_ACCEPT_UPDATE=1`, que aceita a janela sem mostrá-la). Aqui ficam os cliques. **Ambiente diferente da seção 1:** o MO2 roda como AppImage, no host (sem o box), a partir de um AppImage gravado numa pasta sua, por exemplo `~/Applications/ModOrganizer.AppImage`.

Preparação (servidor local, sem GitHub): uma pasta `srv/` com `srv/repos/FM0Ura/ModOrganizerLinux/releases` (a lista de releases no formato da API, um release `v2.5.2.2` com os assets `ModOrganizer-2.5.2.2-x86_64.AppImage` e `SHA256SUMS`, `browser_download_url` em `http://127.0.0.1:8000/download/...`; o `run.sh` gera um modelo), `srv/download/` com o AppImage novo (build com `MO2_VERSION=2.5.2.2`) e o `SHA256SUMS` dele (`sha256sum ModOrganizer-2.5.2.2-x86_64.AppImage > SHA256SUMS`), e `python3 -m http.server --bind 127.0.0.1 --directory srv 8000`. Instalado: um AppImage com `MO2_VERSION=2.5.2.1`. Iniciar com `MO2_UPDATE_API_URL=http://127.0.0.1:8000 ~/Applications/ModOrganizer.AppImage` (variável só de desenvolvimento).

- [ ] Com **Check for updates** ligado em Settings, o botão de atualização aparece na barra; clique: a janela "Update" mostra 2.5.2.1 → v2.5.2.2 e as notas do release.
- [ ] **Cancel**: nada é baixado; nenhum `.ModOrganizer.AppImage.update-*` em `~/Applications`.
- [ ] Clique de novo e aceite: barra de progresso, o MO2 fecha sozinho e abre de novo; **Help → About** mostra 2.5.2.2. `~/Applications/ModOrganizer.AppImage` continua com esse nome e executável (`ls -l`); a mesma instância abriu.
- [ ] Com um jogo ou ferramenta rodando pelo MO2 (VFS montado), clique em atualizar. Esperado: aviso "Programs started by Mod Organizer are still running. Close the running programs first…", nada baixado. Feche o programa e repita: a atualização segue.
- [ ] Troque o `SHA256SUMS` do servidor por um hash errado e repita (com o AppImage 2.5.2.1 de volta). Esperado: aviso "Update failed … does not match the SHA256SUMS of the release" com link para a página do release; o AppImage antigo intacto, nada em `.update-*`, o MO2 continua aberto.
- [ ] Tire o `SHA256SUMS` da lista de assets do release. Esperado: aviso "The release has no SHA256SUMS…" com o link, sem download.
- [ ] `chmod a-w ~/Applications` e reinicie o MO2. Esperado: sem botão de atualização (log de depuração "not checking for updates, the folder of … is not writable"). Volte com `chmod u+w`.
- [ ] Sem `MO2_UPDATE_API_URL` e com o repositório privado: sem botão, e o log de depuração diz "no releases found, or the request for them failed" (sem janela de erro).
- [ ] Fora do AppImage (`distrobox enter mo2-run -- install/bin/ModOrganizer`): log "not checking for updates, not running from an AppImage".
