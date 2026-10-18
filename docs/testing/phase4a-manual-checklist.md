# Fase 4a — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-18-phase4a-nexus-nxm-keyring-design.md`](../superpowers/specs/2026-10-18-phase4a-nexus-nxm-keyring-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 compilado na distrobox `mo2-run` (`./build.sh`, resultado em `install/`) e iniciado **pelo box**: `distrobox enter mo2-run -- <repo>/install/bin/ModOrganizer`. Firefox (Flatpak) logado no Nexus. Instância de Skyrim SE das fases anteriores.

**Antes de começar:** no host, `xdg-mime query default x-scheme-handler/nxm` (anote; esperado: vazio) e, no KWallet Manager, nenhuma entrada `Mod Organizer 2: …`.

| # | Passo | Esperado | Resultado |
|---|---|---|---|
| 1 | Configurações → Nexus → conectar (OAuth). Autorizar no navegador. | O navegador do host abre; depois de autorizar, o MO2 mostra a conta. | |
| 1b | Fechar e reabrir o MO2. | Continua conectado, sem pedir login. No KWallet Manager, entrada `Mod Organizer 2: ModOrganizer2_NEXUS_OAUTH_TOKENS`. | |
| 2 | Desconectar do Nexus nas configurações. | A entrada some do KWallet. | |
| 2b | Conectar por API key (colar a chave). Fechar e reabrir. | Continua conectado; entrada `…_APIKEY` no KWallet. | |
| 3 | No host: `xdg-mime query default x-scheme-handler/nxm`; `cat ~/.local/share/applications/modorganizer2-nxm-handler.desktop`. | `modorganizer2-nxm-handler.desktop`; `Exec=distrobox enter mo2-run -- <caminho>/ModOrganizer %u`. | |
| 4 | Com o MO2 aberto: no site do Nexus, "Mod Manager Download" de um mod pequeno de Skyrim SE. | O Firefox pode perguntar com qual programa abrir (aceite e lembre); o download aparece na aba Downloads e termina. | |
| 4b | Fechar o MO2 e repetir com outro mod. | O MO2 abre na instância e baixa. | |
| 5 | Instalar o mod do item 4 e um mod FOMOD baixado do mesmo jeito; ativar. | Instalação normal (FOMOD com o assistente); arquivos na aba Data. | |
| 6 | No host: `xdg-mime default firefox.desktop x-scheme-handler/nxm` (simula outro handler). Reabrir o MO2. Depois, Configurações → Nexus → "Associate with Download Manager Links". | Reabrir não troca (`xdg-mime query …` = `firefox.desktop`); o botão mostra "now handles nxm:// links" e o query volta a `modorganizer2-nxm-handler.desktop`. | |
| 7 | `distrobox enter mo2-run -- env DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent <repo>/install/bin/ModOrganizer`; conectar por API key. | Aviso "Login not saved" uma vez; conectado durante a sessão; ao reabrir normalmente, a chave do item 2b continua no KWallet (nada foi apagado). | |

**Ao terminar:** registre aqui o resultado de cada item e, em "Observações", o que precisou de correção.

## Observações
