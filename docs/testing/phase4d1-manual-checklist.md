# Fase 4d.1 — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-21-phase4d1-python-design.md`](../superpowers/specs/2026-10-21-phase4d1-python-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`. Instância de Skyrim SE das fases anteriores. Pacotes do box: `python3 python3-pyqt6` (README, "Plugins Python").

**Build:** o `install/` do worktree `ModOrganizerLinux-4d1` (`./build.sh` na distrobox `mo2-run`) ou, depois de um run Linux verde do PR, o artefato `mo2-linux-install` desse run (restaure a permissão de execução de `bin/ModOrganizer` e `bin/mo2-vfs`).

## 1. Python carregado

- [ ] Abrir o MO2. Esperado: nenhum aviso do "Python Proxy" no ícone de notificações; em **Configurações → Plugins**, o "Python Proxy" e os plugins Python (DDS Preview, Form 43 Checker, Script Extender Plugin Checker, Configurator, Wizard Installer, FNIS Tool/Patches/Reset) aparecem habilitados.
- [ ] No log (`logs/mo_interface.log`): nenhuma linha `failed to init python`.

## 2. Pré-visualização de DDS

- [ ] Na aba **Data**, abrir a pré-visualização de um `.dds` do mod `Mod2_Textures` (duplo clique ou botão direito → Preview). Esperado: a textura aparece (canais, zoom), sem erro de OpenGL.

## 3. Verificadores

- [ ] Com um plugin `.esp` de formato 43 (Oldrim) ativo, se houver: o ícone de notificações mostra o aviso do Form 43 Checker. Sem um, conferir só que não há erro do plugin no log.
- [ ] Script Extender Plugin Checker: depois de rodar o SKSE uma vez (Fase 3), sem erro no log; com uma DLL do SKSE que não carregou (opcional), o aviso aparece.

## 4. Configurator

- [ ] **Ferramentas → Configurator** (ou o nome que o menu mostrar). Esperado: abre com os INIs do perfil (`Skyrim.ini`, `SkyrimPrefs.ini`); alterar um valor, salvar, e o INI do perfil muda.

## 5. Instalador BAIN com wizard

- [ ] Compactar a fixture: `cd tests/fixtures/bain-wizard && 7z a /tmp/bain-wizard.7z ./*`. No MO2, **Instalar mod** com `/tmp/bain-wizard.7z`. Esperado: o diálogo do wizard pergunta "Choose the option"; escolher "Option B"; o mod instalado tem `core.txt` e `b.txt` (não `a.txt`).

## 6. Isolamento

- [ ] No host, `pip install --user requests` (ou qualquer pacote ausente do box); abrir o MO2. Esperado: nada muda (o MO2 não usa o pacote). Opcional: um plugin de teste com `import requests` falha com `ModuleNotFoundError` no log. Desinstale depois (`pip uninstall requests`).

## 7. FNIS (opcional)

- [ ] Com o FNIS instalado como mod: **FNIS Tool** no menu de ferramentas pede a pasta de saída (um mod), roda o `GenerateFNISforUsers.exe` por Proton com o VFS, e a saída vai para o mod escolhido.

## 8. Regressão

- [ ] SKSE inicia o jogo com os mods (Fase 3); LOOT ordena (4b); pré-visualização de `.txt`/imagens (4c.1).
