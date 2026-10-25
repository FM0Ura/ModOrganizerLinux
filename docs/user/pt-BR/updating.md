# Atualização

**Português** · [English](../en/updating.md) · Anterior: [Primeiros passos](first-steps.md) · Próximo: [Jogos](games.md)

Os releases saem na [página de releases](https://github.com/FM0Ura/ModOrganizerLinux/releases).
Há três jeitos de atualizar; use o que preferir.

## Dentro do Mod Organizer

Com **Settings, Check for updates** ligado, o Mod Organizer consulta os releases de
`FM0Ura/ModOrganizerLinux` ao abrir. Havendo versão mais nova, aparece um botão de
atualização na barra. Clique para ver a versão e as notas do release e aceite:

1. ele baixa o AppImage novo ao lado do antigo e confere o SHA-256 contra o `SHA256SUMS`
   do release;
2. troca o seu arquivo **no lugar** (mesmo nome, mesmas permissões), então a entrada do menu
   e o tratador de `nxm://` continuam valendo;
3. fecha e abre de novo já na versão nova (Help, About mostra), na instância que estava
   aberta. Um link (`nxm://`, `moshortcut://`) ou um comando com que ele foi aberto não é
   tratado de novo. Se o fechamento for cancelado (downloads ou programas ainda rodando), a
   versão nova abre na próxima vez que você abrir o Mod Organizer.

Uma atualização interrompida por uma falha pode deixar um arquivo oculto
`.<nome>.update-<número>` ao lado do AppImage; o Mod Organizer o remove na próxima vez que
consulta as atualizações.

Ele não atualiza quando:

- um programa iniciado pelo Mod Organizer ainda está rodando (a pasta virtual está montada).
  O MO2 avisa e não instala nada (um arquivo que ele já tinha baixado é descartado). Nada
  espera nem continua depois: feche os programas e clique no botão de atualização de novo;
- a pasta do AppImage não é gravável. O MO2 ainda consulta os releases e mostra o botão de
  atualização de sempre, mas clicar nele só mostra um aviso com o link da página do
  release; nada é baixado nem trocado. Troque o arquivo você mesmo, ou atualize pelo Gear
  Lever;
- o Mod Organizer não está rodando de um AppImage;
- o checksum não confere ou o release não tem `SHA256SUMS`: ele avisa, aponta o release e
  deixa o arquivo antigo como estava.

O modo offline e a opção de pré-lançamentos valem como no Windows.

## Com o Gear Lever ou o AppImageUpdate

O AppImage leva a informação de atualização (`gh-releases-zsync`), então o Gear Lever mostra
"atualização disponível" e baixa só a parte do arquivo que mudou. Pelo terminal, o
AppImageUpdate faz o mesmo:

```sh
appimageupdatetool ~/Applications/ModOrganizer.AppImage
```

## À mão

Baixe o novo `ModOrganizer-<versão>-x86_64.AppImage` da página de releases, confira com
`sha256sum -c SHA256SUMS --ignore-missing` e troque o arquivo antigo.

Suas instâncias, mods e configurações não estão no AppImage; atualizar nunca mexe nelas.

Um release novo começa como rascunho, que o mantenedor testa e publica; os mecanismos de
atualização só enxergam releases publicados.

Próximo: [Jogos](games.md).
