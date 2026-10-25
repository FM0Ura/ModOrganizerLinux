<a id="readme-top"></a>

<div align="center">

<img src="https://capsule-render.vercel.app/api?type=waving&color=gradient&height=200&section=header&text=Mod%20Organizer%202%20for%20Linux&fontSize=44&fontAlignY=35&animation=fadeIn" width="100%" alt="Mod Organizer 2 for Linux"/>

<p><b>O gerenciador de mods Mod Organizer 2 como um AppImage nativo para Linux, para jogos da Steam rodando pelo Proton.</b></p>

[![License](https://img.shields.io/badge/license-GPL--3.0-blue?style=for-the-badge)](LICENSE)
[![Release](https://img.shields.io/github/v/release/FM0Ura/ModOrganizerLinux?style=for-the-badge&include_prereleases)](https://github.com/FM0Ura/ModOrganizerLinux/releases)
[![Linux CI](https://img.shields.io/github/actions/workflow/status/FM0Ura/ModOrganizerLinux/linux.yml?style=for-the-badge&label=linux%20ci)](https://github.com/FM0Ura/ModOrganizerLinux/actions)
[![Last commit](https://img.shields.io/github/last-commit/FM0Ura/ModOrganizerLinux?style=for-the-badge)](https://github.com/FM0Ura/ModOrganizerLinux/commits)

[English](README.md) · **Português**

[Releases](https://github.com/FM0Ura/ModOrganizerLinux/releases) · [Guia do usuário](docs/user/pt-BR/install.md) · [Reportar bug](https://github.com/FM0Ura/ModOrganizerLinux/issues/new)

</div>

<br>

<details>
<summary><b>📋 Sumário</b></summary>

- [📖 Sobre](#-sobre)
- [✨ Funcionalidades](#-funcionalidades)
- [🧰 Tecnologias](#-tecnologias)
- [🚀 Como rodar](#-como-rodar)
- [🔄 Atualização](#-atualização)
- [🎮 Jogos que funcionam](#-jogos-que-funcionam)
- [🐛 Problemas conhecidos](#-problemas-conhecidos)
- [🤝 Contribuindo](#-contribuindo)
- [📝 Licença](#-licença)

</details>

## 📖 Sobre

O [Mod Organizer 2](https://github.com/ModOrganizer2/modorganizer) é o gerenciador de mods que
mantém a pasta do jogo limpa, mostrando ao jogo uma visão virtual e mesclada dos seus mods.
Este é um port nativo para Linux: ele gerencia mods de jogos da Steam rodando pelo Proton e
sai como um AppImage único, então você baixa e executa, sem container nem ambiente de
compilação. O Bazzite é o desktop de referência. A versão do Windows continua compilando
a partir do mesmo código, contra o `uibase` do upstream.

<!-- TODO: adicionar uma captura da janela principal (docs/assets/main-window.png) -->

## ✨ Funcionalidades

- 📦 **Um arquivo só** — um AppImage com o próprio Qt, QtWebEngine, Python e bibliotecas
- 🔀 **Sistema de arquivos virtual** — mods mesclados para o jogo por FUSE, nada gravado na pasta do jogo
- 🧩 **Plugins** — os jogos da Bethesda, FOMOD, instaladores BAIN e bundle, pré-visualizações, as ferramentas de INI, os plugins Python e cerca de 75 jogos da comunidade (`basic_games`)
- 📋 **LOOT** — ordenação de plugins com a libloot
- 🌐 **Nexus Mods** — login guardado no Secret Service (KWallet, GNOME Keyring) e links `nxm://`
- 🧳 **BSA/BA2** — criação de arquivos com o BSA Packer
- 🔄 **Atualiza sozinho** — pelos releases do GitHub, ou pelo Gear Lever

## 🧰 Tecnologias

<p align="center">
  <img src="https://skillicons.dev/icons?i=cpp,cmake,qt,rust,python,linux&theme=dark" alt="C++, CMake, Qt, Rust, Python, Linux" />
</p>

C++ e Qt 6 (o aplicativo), Rust (o auxiliar FUSE `mo2-vfs`), Python 3.14 com PyQt6 (plugins),
empacotados como AppImage no Fedora 44.

## 🚀 Como rodar

### Requisitos

- Um desktop Linux de 64 bits com **glibc 2.43 ou mais nova**
- Steam com Proton, `umu-run` e FUSE 3, que **não** vêm no pacote
  ([dependências do host](docs/user/pt-BR/install.md#dependências-do-host))

### Distribuições testadas

Cada release é iniciada num container limpo de cada uma destas (até a janela principal); o
jogo de verdade é testado no Bazzite.

| Distribuição | glibc |
|---|---|
| Bazzite (desktop de referência) | 2.43 |
| Fedora 44 | 2.43 |
| Arch Linux (rolling) | rolling |
| Ubuntu 26.04 LTS | 2.43 |

O Ubuntu 24.04 é antigo demais (glibc 2.39).

### Instalação

Baixe o `ModOrganizer-<versão>-x86_64.AppImage` (cerca de 253 MB) na
[página de releases](https://github.com/FM0Ura/ModOrganizerLinux/releases).

**Com o Gear Lever** (Bazzite): abra o arquivo no Gear Lever, depois **Unlock** e **Move to the app menu**.

**Direto:**

```bash
chmod +x ModOrganizer-2.5.2.1-x86_64.AppImage
./ModOrganizer-2.5.2.1-x86_64.AppImage
```

> [!TIP]
> Rode o jogo uma vez pela Steam antes de criar a instância, para o Proton criar o prefixo.
> Nunca rode o Mod Organizer como root.

Mais no [guia de instalação](docs/user/pt-BR/install.md) e nos
[primeiros passos](docs/user/pt-BR/first-steps.md).

### Dependências do host

O Mod Organizer abre e gerencia mods sem elas; o **Executar** precisa delas, e o MO2 diz o
que falta.

| Programa | Para |
|---|---|
| Steam com Proton | o jogo e o prefixo dele |
| `umu-run` ([umu-launcher](https://github.com/Open-Wine-Components/umu-launcher)) | rodar programas do Windows no prefixo |
| FUSE 3 (`fusermount3`) | a pasta virtual dos mods |

Comandos de instalação por distribuição: [Dependências do host](docs/user/pt-BR/install.md#dependências-do-host).

## 🔄 Atualização

- **Dentro do Mod Organizer:** com *Check for updates* ligado, aparece um botão de
  atualização quando há um release novo. Ele baixa, confere o SHA-256 contra o `SHA256SUMS`
  do release, troca o arquivo no lugar e reinicia. Espera enquanto um programa iniciado pelo
  Mod Organizer está rodando.
- **Gear Lever ou AppImageUpdate:** o AppImage leva a informação de atualização, então só a
  parte que mudou é baixada.

Detalhes: [Atualização](docs/user/pt-BR/updating.md).

## 🎮 Jogos que funcionam

| Jogo | Situação |
|------|----------|
| Skyrim Special Edition (Steam + Proton) | Funciona: mods pelo sistema de arquivos virtual, SKSE, LOOT e SSEEdit, conferidos nos testes manuais no Bazzite. |
| Cyberpunk 2077 (Steam + Proton) | Funciona: mods pelo sistema de arquivos virtual, conferido à mão no Bazzite. CET e RED4ext precisam de `WINEDLLOVERRIDES` ([como](docs/user/pt-BR/games.md#plugins-python)). |
| NieR:Automata (Steam + Proton) | Funciona: mods pelo sistema de arquivos virtual, conferido à mão no Bazzite. Todo mod passa pelo instalador manual (o plugin, como no upstream, não reconhece a estrutura dos mods). |

Todos os outros jogos só são detectados (os plugins da Bethesda e os cerca de 75 jogos do
`basic_games`) e **não** foram validados. Ferramentas que rodam pelo sistema de arquivos
virtual: SKSE, LOOT, SSEEdit, e BodySlide, Nemesis e Pandora instalados como mods (os dois
últimos têm falhas de exibição; [Jogos e ferramentas](docs/user/pt-BR/games.md)).

## 🐛 Problemas conhecidos

- **Jogo sem teclado no KDE Wayland:** uma regra de janela do KWin para `steam_app_.*` resolve ([como](docs/user/pt-BR/troubleshooting.md#o-jogo-não-recebe-o-teclado-kde-wayland)).
- **Baldur's Gate 3** foi experimentado e tem lacunas: ainda não há ordem de carga no Linux, e um save com acento no nome não foi gravado ([detalhes](docs/user/pt-BR/troubleshooting.md#baldurs-gate-3)).
- Em hosts que não são baseados em Fedora, o navegador embutido não confia nas autoridades certificadoras que você adicionou ao repositório do sistema.
- Uma queda? [Relate com o backtrace](docs/user/pt-BR/troubleshooting.md#relatar-uma-queda) (`coredumpctl` e os símbolos de depuração do release).

Tudo: [Problemas conhecidos](docs/user/pt-BR/troubleshooting.md).

## 🤝 Contribuindo

Compilar a partir do código, o CI, como um release é feito e as variáveis de desenvolvimento
e de teste estão em [docs/dev/building.md](docs/dev/building.md) (em inglês). O plano do port
está no [ROADMAP.md](ROADMAP.md) e o estado atual no [STATE.md](STATE.md). Abra uma issue
antes de uma mudança grande.

## 📝 Licença

Distribuído sob a GNU General Public License v3.0, como o Mod Organizer 2 do upstream. Veja
o [`LICENSE`](LICENSE) para os detalhes.

---

<div align="center">

Feito por **[FM0Ura](https://github.com/FM0Ura)**. Baseado no [Mod Organizer 2](https://github.com/ModOrganizer2/modorganizer).

<p align="right">(<a href="#readme-top">voltar ao topo</a>)</p>

<img src="https://capsule-render.vercel.app/api?type=waving&color=gradient&height=100&section=footer" width="100%" alt=""/>

</div>
