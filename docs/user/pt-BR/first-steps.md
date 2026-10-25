# Primeiros passos

**Português** · [English](../en/first-steps.md) · Anterior: [Instalação](install.md) · Próximo: [Atualização](updating.md)

## Antes de começar

1. Instale as [dependências do host](install.md#dependências-do-host).
2. **Rode o jogo uma vez pela Steam.** O Proton cria o prefixo (as pastas do Windows que o
   jogo usa: documentos, configurações, saves) na primeira vez. O Mod Organizer lê as
   pastas "My Games" e AppData do jogo nesse prefixo
   (`<biblioteca da Steam>/steamapps/compatdata/<app id>/pfx`); sem ele, usa as pastas XDG
   e avisa no log.

## Criar uma instância

1. Abra o Mod Organizer e escolha **Create a new instance**, depois o jogo.
2. O MO2 acha jogos da Steam em `~/.local/share/Steam`, `~/.steam/steam`, na Steam em
   Flatpak e nas bibliotecas extras do `libraryfolders.vdf`. Se o seu jogo está em outro
   lugar, informe a pasta à mão.
3. Escolha onde fica a instância (mods, perfis, downloads). São pastas comuns: você pode
   fazer backup copiando.

Outras lojas (GOG, Epic, EA, Origin) não são detectadas.

## Nexus Mods e links `nxm://`

- **Settings, Nexus**: entre no Nexus Mods. O login fica no Secret Service (KWallet, GNOME
  Keyring); sem um, dura até o Mod Organizer fechar.
- O Mod Organizer se registra como o programa dos links `nxm://` (um arquivo desktop em
  `~/.local/share/applications`) quando nenhum outro os trata; **Settings, Nexus** força
  isso. Depois, o "Mod Manager Download" do site do Nexus abre o Mod Organizer e baixa,
  inclusive instaladores FOMOD.
- Se você mover o AppImage, abra-o uma vez para o registro ser reescrito.

## Rodar o jogo e as ferramentas

Escolha o programa (o jogo, o SKSE, uma ferramenta) na barra e clique em **Run**. O Mod
Organizer monta a pasta virtual com os seus mods, executa o programa do Windows pelo
`umu-run` no prefixo do Proton do jogo e desmonta quando ele termina.

- Um jogo e uma ferramenta não rodam ao mesmo tempo no mesmo prefixo.
- Jogos com uma DLL "proxy" (CET e RED4ext do Cyberpunk 2077, FAR do NieR:Automata, Script
  Extender do Baldur's Gate 3) precisam de um `WINEDLLOVERRIDES`; veja [Jogos](games.md).
- Para BodySlide, Nemesis, Pandora ou xEdit, veja [Jogos](games.md#ferramentas-bodyslide-nemesis-pandora-xedit).
- Se faltar o `umu-run` ou o `fusermount3`, o Mod Organizer avisa.

## Ordenar plugins com o LOOT

**Sort** na lista de plugins roda o LOOT (libloot 0.29.5). Ele usa a pasta de dados do LOOT
em Flatpak, se você tiver, ou `~/.local/share/LOOT`, e baixa a masterlist na primeira vez.

Próximo: [Atualização](updating.md).
