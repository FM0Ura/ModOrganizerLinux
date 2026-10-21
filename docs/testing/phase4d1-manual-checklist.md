# Fase 4d.1 — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-21-phase4d1-python-design.md`](../superpowers/specs/2026-10-21-phase4d1-python-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`. Instância de Skyrim SE das fases anteriores. Pacotes do box: `python3 python3-pyqt6` (README, "Plugins Python").

**Build:** o `install/` do worktree `ModOrganizerLinux-4d1` (`./build.sh` na distrobox `mo2-run`) ou, depois de um run Linux verde do PR, o artefato `mo2-linux-install` desse run (restaure a permissão de execução de `bin/ModOrganizer` e `bin/mo2-vfs`).

## 1. Python carregado

- [ ] Abrir o MO2. Esperado: nenhum aviso do "Python Proxy" no ícone de notificações; em **Configurações → Plugins**, o "Python Proxy" e os plugins Python ("DDS Preview Plugin", "Form 43 Plugin Checker", "Script Extender Plugin Load Checker", "Configurator", "BAIN Wizard Installer", "FNIS Integration Tool", "FNIS Patches Tool" e "FNIS Integration Tool Reset") aparecem habilitados.
- [ ] No log (`logs/mo_interface.log`): nenhuma linha `failed to init python`.

## 2. Pré-visualização de DDS

- [ ] Na aba **Data**, abrir a pré-visualização de um `.dds` do mod `Mod2_Textures` (duplo clique ou botão direito → Preview). Esperado: a textura aparece (canais, zoom), sem erro de OpenGL.

## 3. Verificadores

- [ ] Com um plugin `.esp` de formato 43 (Oldrim) ativo, se houver: o ícone de notificações mostra o aviso do "Form 43 Plugin Checker". Sem um, conferir só que não há erro do plugin no log.
- [ ] "Script Extender Plugin Load Checker": depois de rodar o SKSE uma vez (Fase 3), sem erro no log; com uma DLL do SKSE que não carregou (opcional), o aviso aparece.

## 4. Configurator

- [ ] **Ferramentas → Configurator** (ou o nome que o menu mostrar). Esperado: abre com os INIs do perfil (`Skyrim.ini`, `SkyrimPrefs.ini`); alterar um valor, salvar, e o INI do perfil muda.

## 5. Instalador BAIN com wizard

- [ ] Compactar a fixture: `cd tests/fixtures/bain-wizard && 7z a /tmp/bain-wizard.7z ./*`. No MO2, **Instalar mod** com `/tmp/bain-wizard.7z`. Esperado: o diálogo do wizard pergunta "Choose the option"; escolher "Option B"; o mod instalado tem `core.txt` e `b.txt` (não `a.txt`).

## 6. Isolamento

A prova automática é o teste `Isolation.UserPackagesAreInvisible` (ctest do `plugin_python`); aqui se confirma o mesmo no MO2 de verdade, com um plugin de teste descartável. `<pasta do build>` é o `install/` do worktree. Com o MO2 fechado:

- [ ] Descobrir a pasta de pacotes do usuário do box e criar nela um pacote que não existe no box (o `$HOME` do box é o do host):

```sh
SITE=$(distrobox enter mo2-run -- python3 -c 'import site; print(site.getusersitepackages())')
mkdir -p "$SITE/mo2_isolation_probe" && echo 'X = 1' > "$SITE/mo2_isolation_probe/__init__.py"
distrobox enter mo2-run -- python3 -c 'import mo2_isolation_probe; print("box python sees it:", mo2_isolation_probe.X)'
```

  Esperado: a última linha imprime `box python sees it: 1` (o Python comum do box enxerga o pacote; sem isso o teste não prova nada).
- [ ] Criar o plugin de teste e abrir o MO2 pelo box:

```sh
cat > <pasta do build>/bin/plugins/isolation_probe.py <<'PY'
import importlib.util
import sys

seen = importlib.util.find_spec("mo2_isolation_probe") is not None
with open("/tmp/mo2_isolation_probe.txt", "w") as f:
    f.write("visible\n" if seen else "invisible\n")
    f.write("\n".join(sys.path) + "\n")


def createPlugins():
    return []
PY
rm -f /tmp/mo2_isolation_probe.txt
distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer
```

  Depois de o MO2 abrir (pode fechá-lo): `head -1 /tmp/mo2_isolation_probe.txt`. **Esperado: `invisible`.** Se mostrar `visible`, o isolamento falhou (reprove o item). O resto do arquivo é o `sys.path` do MO2: só a biblioteca padrão e o `site-packages` do sistema (`/usr/lib64/python3.14`, `/usr/lib/python3.14`) e pastas do MO2, nada sob `$HOME` nem `/usr/local`. Se o arquivo não existir, o plugin de teste não foi carregado (confira o item 1).
- [ ] Limpeza:

```sh
rm -rf "$SITE/mo2_isolation_probe" <pasta do build>/bin/plugins/isolation_probe.py /tmp/mo2_isolation_probe.txt
```

## 7. FNIS (opcional)

- [ ] Com o FNIS instalado como mod: **FNIS/Run FNIS** no menu de ferramentas (também há **FNIS/Configure FNIS Patches** e **FNIS/Reset FNIS Settings**) pede a pasta de saída (um mod), roda o `GenerateFNISforUsers.exe` por Proton com o VFS, e a saída vai para o mod escolhido.

## 8. Regressão

- [ ] SKSE inicia o jogo com os mods (Fase 3); LOOT ordena (4b); pré-visualização de `.txt`/imagens (4c.1).
