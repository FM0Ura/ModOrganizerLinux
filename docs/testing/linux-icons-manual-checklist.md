# Ícones de arquivos e executáveis no Linux: checklist manual

Spec: [`../superpowers/specs/2026-10-27-linux-icons-design.md`](../superpowers/specs/2026-10-27-linux-icons-design.md) · Plano: [`../superpowers/plans/`](../superpowers/plans/)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado pelo box com o AppImage local (v2.5.2.4) ou o build do worktree `fix-linux-icons`. Skyrim SE e Stellar Blade instalados pela Steam.

## 0. Tema de ícones

- [ ] Anotar o tema de ícones do KDE ativo (Configurações do sistema > Aparência > Ícones): ______. Não é preciso trocar de tema.

## 1. Aba Data (Skyrim SE)

- [ ] `.esp`, `.esm`, `.bsa`, `.nif`, `.dds`, `.pex`, `.ini`, `.txt` e `.dll` aparecem com ícones diferentes entre si (plugins, pacote, malha, imagem, script, configuração, texto, biblioteca).

## 2. Lista de executáveis

- [ ] SKSE, LOOT, BodySlide e o jogo mostram cada um o próprio ícone (o embutido no `.exe`). Um `.exe` sem ícone embutido mostra o ícone genérico de executável.

## 3. Ícone do jogo

- [ ] O ícone do Skyrim SE aparece na barra de tarefas/instância.

## 4. Stellar Blade

- [ ] `.pak`, `.utoc` e `.ucas` aparecem com o ícone de pacote.

## 5. Desempenho

- [ ] Abrir a aba Data com muitos arquivos: sem travamento perceptível; os ícones aparecem logo depois da lista.

## 6. Genéricos

- [ ] Um `.lnk` e um arquivo de extensão desconhecida aparecem com o ícone genérico de arquivo. Um `.ico` mostra a própria imagem.
