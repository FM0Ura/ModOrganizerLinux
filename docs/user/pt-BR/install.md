# Instalando o Mod Organizer 2 para Linux

**Português** · [English](../en/install.md) · Próximo: [Primeiros passos](first-steps.md)

O Mod Organizer 2 para Linux é um arquivo só, um AppImage. Baixe, torne executável e
rode, ou deixe o Gear Lever cuidar dele. Ele traz o próprio Qt, QtWebEngine, Python e
bibliotecas, então você não precisa de ambiente de desenvolvimento nem de distrobox.

## O que você precisa

- Um desktop Linux de 64 bits (x86_64) com sessão gráfica (Wayland ou X11) e **glibc 2.43
  ou mais nova**. Confira com `ldd --version`. O AppImage é compilado no Fedora 44, então
  distribuições mais antigas não são suportadas (o Ubuntu 24.04 tem glibc 2.39 e é antigo
  demais).
- A Steam, com o Proton do jogo que você quer modificar. O Mod Organizer gerencia mods de
  jogos da Steam rodando pelo Proton; outras lojas não são suportadas.
- Os programas listados em [Dependências do host](#dependências-do-host).

### Distribuições testadas

Cada release é iniciada num container limpo de cada uma destas antes de ser publicada
(até a janela principal, sem tela; o jogo de verdade só é testado no Bazzite):

| Distribuição | glibc | Observações |
|---|---|---|
| Bazzite (base Fedora 44) | 2.43 | O desktop de referência; os jogos são testados aqui |
| `fedora:44` | 2.43 | Teste de fumaça em container |
| `archlinux:latest` | rolling | Teste de fumaça em container |
| `ubuntu:26.04` (LTS) | 2.43 | Teste de fumaça em container; a 24.04 é antiga demais |

Outras com glibc 2.43 ou mais nova devem funcionar, mas não são testadas.

## Baixar

Pegue o `ModOrganizer-<versão>-x86_64.AppImage` mais recente (cerca de 253 MB) na
[página de releases](https://github.com/FM0Ura/ModOrganizerLinux/releases). A versão tem
quatro números, como `2.5.2.1`: a versão do Mod Organizer original mais o número do port.
O release traz também o `SHA256SUMS`; para conferir o download:

```sh
sha256sum -c SHA256SUMS --ignore-missing
```

## Executar

### Com o Gear Lever (Bazzite)

Abra o arquivo baixado no [Gear Lever](https://github.com/mijorus/gearlever) (o Bazzite já
o traz), clique em **Unlock** e depois em **Move to the app menu**: ele move o AppImage para
a pasta de aplicativos e cria uma entrada no menu. O Gear Lever também pode atualizá-lo
depois (veja [Atualização](updating.md)).

### Direto

```sh
mkdir -p ~/Applications
mv ~/Downloads/ModOrganizer-2.5.2.1-x86_64.AppImage ~/Applications/ModOrganizer.AppImage
chmod +x ~/Applications/ModOrganizer.AppImage
~/Applications/ModOrganizer.AppImage
```

Deixe o arquivo numa pasta onde você pode gravar (sua home): o atualizador do próprio MO2
o substitui no lugar. Nunca rode o Mod Organizer como root nem com `sudo`.

Se o AppImage não abrir por falta do FUSE, instale o FUSE 3 (abaixo) ou rode-o extraído:
`./ModOrganizer.AppImage --appimage-extract-and-run`.

## Dependências do host

Estes programas **não** vêm no AppImage. O Mod Organizer abre e gerencia mods sem eles; só
o **Executar** precisa deles, e o MO2 confere antes de executar um programa, diz o que
falta e aponta para esta página.

| Programa | Para que serve |
|---|---|
| Steam, com o Proton | O jogo e o prefixo do Proton em que ele roda. Rode o jogo uma vez pela Steam para o prefixo existir |
| `umu-run` ([umu-launcher](https://github.com/Open-Wine-Components/umu-launcher)) | Roda os programas do Windows (jogo, SKSE, ferramentas) no prefixo do Proton |
| FUSE 3 (`fusermount3`) | Monta a pasta virtual que mostra ao jogo os mods mesclados |

Veja o que você já tem:

```sh
command -v umu-run fusermount3 steam
```

Instale-os com o gerenciador de pacotes da sua distribuição. O Bazzite já traz a Steam e o
`umu-run`.

| Distribuição | FUSE 3 | umu-launcher |
|---|---|---|
| Fedora | `sudo dnf install fuse3` | `sudo dnf install umu-launcher` |
| Arch | `sudo pacman -S fuse3` | `umu-launcher` (repositório oficial ou AUR) |
| Ubuntu | `sudo apt install fuse3` | sem pacote no repositório do Ubuntu: use o `.deb`/zipapp das [releases do umu-launcher](https://github.com/Open-Wine-Components/umu-launcher/releases) |

### Bibliotecas gráficas e do sistema

O AppImage usa a pilha gráfica do seu sistema, para os drivers combinarem com a sua placa.
Uma instalação normal de desktop já tem tudo. Se uma instalação mínima não abrir, estes são
os pacotes que os testes em container instalam (todo o resto está dentro do AppImage):

- **Fedora:** `mesa-libGL mesa-libEGL libglvnd-opengl libglvnd-glx fontconfig freetype libxkbcommon libxkbcommon-x11 libX11 libX11-xcb libxcb dbus-libs libwayland-client libwayland-cursor libwayland-egl mesa-libgbm libdrm vulkan-loader dejavu-sans-fonts ca-certificates findutils`
- **Arch:** `mesa libglvnd fontconfig freetype2 libxkbcommon libxkbcommon-x11 libx11 libxcb dbus wayland libdrm vulkan-icd-loader ttf-dejavu ca-certificates findutils`
- **Ubuntu:** `ca-certificates libgl1 libegl1 libopengl0 libglx0 libfontconfig1 libfreetype6 libx11-6 libx11-xcb1 libxcb1 libxkbcommon0 libxkbcommon-x11-0 libdbus-1-3 libwayland-client0 libwayland-cursor0 libwayland-egl1 libgbm1 libdrm2 libvulkan1 fonts-dejavu-core`

Os logins do Nexus e da Steam ficam no Secret Service (KWallet ou GNOME Keyring). Sem um,
são lembrados só até o Mod Organizer fechar.

Próximo: [Primeiros passos](first-steps.md).
