# Fase 4a — Nexus, `nxm://` e keyring — Design

- **Data:** 2026-10-18
- **Status:** aguardando revisão
- **Spec geral:** [`2026-10-14-mo2-linux-port-design.md`](2026-10-14-mo2-linux-port-design.md) (§3.3 resume a Fase 4; este documento a substitui onde divergirem)
- **Fase anterior:** [`2026-10-17-phase3-steam-proton-design.md`](2026-10-17-phase3-steam-proton-design.md) (ponte `vfsbridge` para o host, usada aqui para o `xdg-mime`)
- **Branch:** `fase-4a-nexus` (a partir de `main`)

## 1. Objetivo

Item 3 do critério de sucesso do projeto (spec geral §1.1): baixar mods do Nexus por links `nxm://` e instalá-los, inclusive FOMOD. Para isso, o login do Nexus precisa ser lembrado entre execuções (keyring) e o MO2 precisa ser o programa que o sistema chama para links `nxm://`. Meta: no Bazzite, com o MO2 no distrobox `mo2-run`, o usuário faz login no Nexus uma vez, clica em "Mod Manager Download" no Firefox e o mod aparece baixado na aba Downloads, pronto para instalar.

### 1.1 Divisão da Fase 4

A Fase 4 (Ecossistema) foi dividida em subfases, cada uma com spec, plano, CI, teste manual e merge próprios, na ordem do critério de sucesso:

| Sub | Conteúdo | Critério (§1.1 da spec geral) |
|---|---|---|
| **4a** | `nxm://` + Nexus (login, downloads) + keyring (Secret Service) | 3 |
| 4b | LOOT: `lootcli` nativo com libloot, masterlist, `plugins.txt` no prefixo | 4 |
| 4c | Ferramentas pelo VFS (BodySlide, Nemesis/Pandora, xEdit de outros jogos) + plugins C++ restantes (BAIN, previews, checagens) + pendências adiadas da Fase 2 | 6 |
| 4d | `plugin_python` (pybind11 + Python + PyQt6) e plugins Python (incl. `basic_games`) | "todas as funcionalidades" |
| 4e | Criação de BSA/BA2 nativa | "todas as funcionalidades" |

### 1.2 Decisões desta fase

| Tema | Decisão |
|---|---|
| Onde guardar credenciais | Secret Service do freedesktop (KWallet no Plasma, GNOME Keyring), pela **libsecret** com a API síncrona. Descartados: QtKeychain (API assíncrona, exigiria `QEventLoop` aninhado na interface; dependência extra) e QtDBus manual (reimplementar sessões e prompts do protocolo). |
| Sem Secret Service | Credenciais só em memória, como hoje, com um aviso por sessão. Nenhum segredo vai para o disco. |
| Registro do handler `nxm` | Como no Windows: ao abrir, registra **só se nenhum programa** trata `nxm://` (ou se o handler já é o nosso, para atualizar o caminho); o botão das configurações do Nexus força o registro. |
| Ferramenta de registro | `xdg-mime`, rodando **no host** (por `distrobox-host-exec` quando o MO2 está em container), para valer a configuração do ambiente gráfico do host. |
| Lançador do handler | `.desktop` em `~/.local/share/applications` chamando o MO2 com o link; no distrobox, por `distrobox enter`. |
| Instalações | Uma instalação do MO2 trata todos os links `nxm://` (a última registrada). |

### 1.3 Ambiente de referência (verificado em 2026-10-18)

- Host Bazzite com KDE Plasma 6 (Wayland), Firefox em Flatpak, KWallet com `org.freedesktop.secrets` (`org.kde.secretservicecompat`) na sessão, `libsecret` 0.21.8 instalada; nenhum handler de `x-scheme-handler/nxm` registrado.
- No distrobox `mo2-run`: `DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus` alcança o Secret Service do host; `/usr/local/bin/xdg-open` é o script do distrobox que abre no host; `xdg-mime` existe (`/usr/bin/xdg-mime`); a rede é a do host.

### 1.4 Fora de escopo

- Handler `modl://`: no upstream ele só existe dentro do `nxmhandler.exe` e o MO2 não trata esses links; no Linux, `registerAsMODLHandler` só encadeia o registro do `nxm`.
- Escolha de qual instalação do MO2 recebe os links de cada jogo (o `nxmhandler.exe` do Windows tem essa tela) e remoção do registro.
- Migração de credenciais: no Linux elas nunca foram gravadas.
- `downloadhandler.ini` (`noregister`) do `nxmhandler.exe`: não existe no Linux.

## 2. Arquitetura

```
Firefox (Flatpak) ──portal OpenURI──▶ modorganizer2-nxm-handler.desktop
                                         │ Exec=distrobox enter mo2-run -- <ModOrganizer> %u
                                         ▼
                               ModOrganizer nxm://…  ──MOMultiProcess──▶ MO2 aberto
                                         │ (sem MO2 aberto)                │
                                         ▼                                 ▼
                               abre a última instância      OrganizerCore::downloadRequestedNXM
                                                                           │
                                                          DownloadManager (API do Nexus, login
                                                          pelas credenciais do keyring)
```

| Unidade | Arquivo | Papel |
|---|---|---|
| Credenciais | `src/linux/settingsutilities_linux.cpp` (reescrito) + `src/linux/secretstore_linux.{h,cpp}` (novo) | `getWindowsCredential`/`addWindowsCredential`/`deleteWindowsCredential` sobre a libsecret, com volta ao mapa em memória |
| Handler `nxm` | `src/linux/nxmhandler_linux.{h,cpp}` (novo) | Conteúdo do `.desktop`, linha `Exec`, decisão de registrar, execução do `xdg-mime` |
| Configurações | `src/settings.cpp` | `NexusSettings::registerAsNXMHandler`, `Settings::registerAsMODLHandler` e `NexusSettings::dump` chamam o código Linux; o código que usa o `nxmhandler.exe` e o registro do Windows fica em `#ifdef _WIN32` |
| Build | `build/Containerfile`, `src/CMakeLists.txt` | `libsecret-devel`; opção `MO2_WITH_LIBSECRET` |

O fluxo de download (`NexusInterface`, `NXMAccessManager`, `DownloadManager`, `MOMultiProcess`, `CommandLine`) não muda por design; o que falhar nele no Linux é corrigido durante a fase (§6).

## 3. Credenciais

### 3.1 Armazenamento

- Esquema libsecret `org.modorganizer.ModOrganizer2` (flags `SECRET_SCHEMA_NONE`), com um atributo string `key`, cujo valor é o nome que o MO2 já usa (`credentialName(key)`: `ModOrganizer2_APIKEY`, `ModOrganizer2_NEXUS_OAUTH_TOKENS`, `ModOrganizer2_steam_password`).
- Gravação: `secret_password_store_sync` na coleção padrão (`SECRET_COLLECTION_DEFAULT`), com rótulo `Mod Organizer 2: <key>`. Leitura: `secret_password_lookup_sync`. Exclusão: `secret_password_clear_sync`; apagar algo que não existe não é erro (como no Windows). Os segredos são UTF-8.
- `settings.cpp` e as chamadas de `setWindowsCredential` não mudam. No Windows nada muda.

### 3.2 Volta à memória

`secretstore_linux` decide, por chamada, entre o Secret Service e o mapa em memória atual:

- Erro da libsecret que indica serviço indisponível (D-Bus sem sessão, nome `org.freedesktop.secrets` ausente, falha de conexão): a partir daí, a sessão inteira do MO2 usa o mapa em memória (a decisão fica guardada; não se tenta o D-Bus a cada chamada). A troca é registrada uma vez com `log::warn`. Na primeira **gravação** feita em memória (é quando o usuário acabou de entrar no Nexus ou no Steam), um `QMessageBox::warning` único diz que o login não será lembrado depois de fechar o MO2; leituras na abertura só registram no log.
- Outros erros (ex. o usuário cancela o desbloqueio da carteira): a operação conta como "sem credencial" na leitura e falha na gravação, com `log::error`; a sessão continua tentando o Secret Service nas próximas chamadas.
- A chamada é síncrona: com a carteira trancada, a interface espera o prompt de desbloqueio do KWallet. No Bazzite, a carteira costuma abrir junto com o login (PAM). Aceito.

### 3.3 Build

- `libsecret-devel` no `build/Containerfile` (e, portanto, no CI Linux).
- Opção CMake `MO2_WITH_LIBSECRET` (padrão ON no Linux, sem efeito no Windows). OFF: o `secretstore_linux` compila só com o mapa em memória, como hoje, e avisa uma vez, como na §3.2.

## 4. Handler `nxm://`

### 4.1 Arquivo `.desktop`

Caminho: `$XDG_DATA_HOME/applications/modorganizer2-nxm-handler.desktop` (padrão `~/.local/share/applications`; a home é a mesma no box e no host).

```ini
[Desktop Entry]
Type=Application
Name=Mod Organizer 2 (nxm links)
Comment=Download Nexus Mods links with Mod Organizer 2
Exec=<lançador> %u
Terminal=false
NoDisplay=true
MimeType=x-scheme-handler/nxm;
```

Lançador (`Exec`), na primeira regra que valer:

1. `$APPIMAGE` definido (Fase 5): o caminho do AppImage.
2. Modo `Host` da `vfsbridge` (MO2 em container) com `CONTAINER_ID` definido (distrobox): `distrobox enter <CONTAINER_ID> -- <applicationFilePath>`.
3. Caso contrário: `<applicationFilePath>`.

Sem `Icon` nesta fase (a instalação Linux não tem um arquivo de ícone; entra com o AppImage na Fase 5). Cada argumento segue as regras de citação da *Desktop Entry Specification* (aspas duplas quando há espaço ou caractere reservado; `"`, `` ` ``, `$` e `\` escapados; `%` dobrado). Modo `Host` sem `CONTAINER_ID` (container que não é distrobox): o registro é recusado com a causa no log ("cannot register the nxm handler from this container"), porque o host não saberia entrar no container.

### 4.2 Registro

Comandos (no host em modo `Host`, como `distrobox-host-exec xdg-mime …`; direto no modo `Direct`):

- consulta: `xdg-mime query default x-scheme-handler/nxm`;
- definição: `xdg-mime default modorganizer2-nxm-handler.desktop x-scheme-handler/nxm`;
- depois: `update-desktop-database <pasta applications>`, se existir; falha ignorada (só log).

Decisão (função pura, testável):

| Handler atual | Sem forçar (ao abrir o MO2) | Forçando (botão) |
|---|---|---|
| nenhum | grava o `.desktop` e define | grava e define |
| `modorganizer2-nxm-handler.desktop` | regrava o `.desktop` (atualiza o caminho) | grava e define |
| outro | nada (log `debug` com o nome do handler) | grava e define |

- Tudo roda de forma assíncrona (`QProcess`, encadeado), para não atrasar a abertura. Uma falha no registro automático vai só para o log (`warn`). Pelo botão, um diálogo mostra a causa (comando ausente, código de saída e stderr).
- `NexusSettings::dump()` no Linux registra o handler atual (`xdg-mime query`) e o `Exec` do nosso `.desktop`, em vez da chave do registro do Windows e do `downloadhandler.ini`.

### 4.3 Link recebido

Código existente: `CommandLine` reconhece `nxm://` (`isNxmLink`); se já há um MO2 aberto, `MOMultiProcess` repassa o link (`QLocalServer` no `TMPDIR`, o mesmo para duas invocações no mesmo box) e ele chega a `OrganizerCore::downloadRequestedNXM`; sem MO2 aberto, o MO2 abre com a última instância e baixa nela. Link de um jogo diferente do da instância: comportamento atual do `DownloadManager`, conferido no teste manual.

## 5. Login do Nexus

Nenhuma mudança planejada: OAuth (`NexusOAuthLogin`, com `QOAuthHttpServerReplyHandler` escutando em `localhost`; o navegador abre por `QDesktopServices`, que no box chama o `xdg-open` do distrobox) e API key (`CredentialsDialog`/configurações) já existem. A fase verifica no Bazzite que ambos funcionam e que as credenciais passam a persistir pela §3; o que falhar é corrigido aqui.

## 6. Erros e riscos

| Risco | Tratamento |
|---|---|
| Redirect do OAuth para `localhost` não chega ao MO2 no box | A rede do distrobox é a do host; conferido no teste manual. Se falhar, corrigir na fase. |
| Firefox Flatpak não repassa `nxm://` ao `.desktop` | O portal `OpenURI` usa os padrões do `mimeapps.list` do host; pode perguntar "abrir com…" na primeira vez (aceito, documentado no checklist). |
| Plasma não enxerga o `.desktop` novo (cache `sycoca`) | `update-desktop-database`; se não bastar, rodar `kbuildsycoca6` no host quando existir (decidido no teste manual). |
| `DownloadManager` no Linux (nomes com `:`/`?`, retomada, pasta de downloads) | Exercitado no teste manual; correções na fase. |
| Interface travada esperando a carteira | Aceito (§3.2). |
| Dois MO2 em boxes diferentes | Fora de escopo: o último registro vence. |

## 7. Testes

Unitários (GoogleTest, `organizer_tests`, sem D-Bus):

- `test_nxmhandler.cpp`: conteúdo do `.desktop`; `Exec` nos três casos (AppImage, distrobox com caminho com espaços e `$`, direto); recusa em container sem `CONTAINER_ID`; tabela de decisão da §4.2 (nenhum, nosso, outro × forçar ou não); comandos gerados nos modos `Direct` e `Host`. Ambiente, raiz do sistema e caminhos injetáveis, como em `test_vfsbridge.cpp`.
- `test_secretstore.cpp`: classificação dos erros da libsecret (indisponível × outro), volta à memória com aviso único, apagar inexistente sem erro, ida e volta pelo mapa em memória.

Integração (CI Linux): ida e volta real pela libsecret contra um `gnome-keyring-daemon` desbloqueado dentro de `dbus-run-session` (pacotes `gnome-keyring` e `dbus-daemon` no container); o teste se marca como pulado se o daemon não existir.

e2e headless (`tests/smoke`, `QT_QPA_PLATFORM=offscreen`, `TMPDIR` privado): um MO2 aberto numa instância de teste recebe um `nxm://` vindo de uma segunda invocação, conferido pelo log (`download requested: nxm://…`); sem rede, o download em si não é exercitado.

Manual no Bazzite (MO2 no box), em `docs/testing/phase4a-manual-checklist.md`:

1. Login OAuth no Nexus; reabrir o MO2 e continuar logado; a entrada `Mod Organizer 2: …` aparece no KWallet.
2. Sair do Nexus remove a entrada; login por API key também persiste.
3. Na primeira abertura, o MO2 vira o handler (`xdg-mime query default x-scheme-handler/nxm` no host).
4. "Mod Manager Download" no site com o MO2 aberto: o download aparece na aba Downloads. Com o MO2 fechado: o MO2 abre e baixa.
5. Instalar um mod comum e um FOMOD baixados assim; ativar e conferir na aba Data.
6. Com outro handler registrado, abrir o MO2 não troca o handler; o botão das configurações do Nexus troca.
7. Sem Secret Service (MO2 iniciado com `DBUS_SESSION_BUS_ADDRESS` inválido): aviso uma vez; login vale só durante a sessão.

## 8. Critérios de pronto

1. Testes unitários, de integração e e2e novos verdes no CI Linux.
2. CI Windows verde (commit com `[windows-ci]`), porque `settings.cpp` muda.
3. Checklist manual aprovado pelo usuário no Bazzite.
4. README, `STATE.md` e `ROADMAP.md` atualizados: a Fase 4 aparece dividida em 4a–4e; saem as limitações "logins não gravados" e "sem `nxm://`".
