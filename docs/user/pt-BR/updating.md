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
3. fecha e abre de novo com os mesmos argumentos, já na versão nova (Help, About mostra).

Ele não atualiza quando:

- um programa iniciado pelo Mod Organizer ainda está rodando (a pasta virtual está montada).
  Feche o programa; a atualização segue;
- a pasta do AppImage não é gravável. O MO2 oferece então a página do release para você
  trocar o arquivo, e não mostra o botão de atualização;
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
