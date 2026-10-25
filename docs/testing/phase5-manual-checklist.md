# Fase 5 — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-24-phase5-distribuicao-design.md`](../superpowers/specs/2026-10-24-phase5-distribuicao-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`, com o `install/` do worktree `ModOrganizerLinux-5`. Duas instâncias ou dois jogos configurados.

## 1. Reiniciar no mesmo processo não derruba o MO2 (spec 3.8)

Antes da correção, trocar de instância/jogo e depois fechar o MO2 dava SIGSEGV com `OzoneUtilQt::getQOpenGLContext` na pilha (STATE.md, "Testes manuais de 2026-10-24", item 1). A correção é `Qt::AA_ShareOpenGLContexts` em `main.cpp`.

- [ ] Abra uma instância, use **Change Game** (ou abra outra instância pelo gerenciador de instâncias): o MO2 reinicia e mostra a janela principal de novo.
- [ ] Feche o MO2 normalmente. Esperado: sai sem queda; `coredumpctl list ModOrganizer` não mostra uma entrada nova.
- [ ] Repita a troca duas vezes seguidas antes de fechar. Esperado: igual.
