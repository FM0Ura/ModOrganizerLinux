# Fase 3 — Checklist manual no Bazzite

Este checklist cobre o item 2 dos critérios de pronto da Fase 3 (spec da Fase 3, §10; casos
do §8). A interface do MO2 só existe em inglês: os nomes de menus e botões abaixo estão em
inglês, exatamente como aparecem na tela. Marque cada caixa; se algo falhar, veja "O que
enviar se falhar" no final.

Spec: [`docs/superpowers/specs/2026-10-17-phase3-steam-proton-design.md`](../superpowers/specs/2026-10-17-phase3-steam-proton-design.md) ·
Plano: [`docs/superpowers/plans/2026-10-17-phase3-steam-proton.md`](../superpowers/plans/2026-10-17-phase3-steam-proton.md)

O que mudou em relação à Fase 2 (não é falha, é esperado):

- O MO2 continua rodando no distrobox `mo2-run`, mas o **`mo2-vfs` e os programas rodam no
  host** (pelo `distrobox-host-exec`). O mount FUSE existe no namespace do **host**: confira
  com `findmnt` **no host**, fora do box. Um programa que só exista dentro do box não
  executa mais pelo MO2.
- Programas Windows (`.exe`, `.bat`, `.cmd`, `.com`) rodam com `umu-run` no prefixo do
  Proton do jogo (`compatdata/<appid>`). O `umu-launcher` precisa existir no host (vem no
  Bazzite).
- O mount só desmonta quando todos os processos do programa **e** o `wineserver` do prefixo
  terminaram (cerca de 3 s depois do jogo fechar).
- **Jogo e ferramenta ao mesmo tempo no mesmo prefixo não são suportados** (a interface do
  MO2 fica travada durante a execução). Rode um de cada vez.
- O `umu` cria o link `pfx/drive_c/users/<usuário> → steamuser` no prefixo da Steam
  (esperado, inofensivo; item 6).
- Um jogo que nunca rodou pela Steam não tem prefixo: rode-o uma vez pela Steam antes.
- Cache de diretórios do Wine após um `update` a quente do VFS não foi testado; a interface
  fica travada durante a execução, então isso só apareceria trocando o estado de mods por
  fora do MO2.
- Valem as limitações das Fases 1 e 2 (sem LOOT, sem Python, sem `nxm://`, logins não gravados).

## 0. Pré-requisitos e preparação

- [x] Bazzite com a **Steam aberta** (a Steam do host precisa estar rodando).
- [x] **Skyrim Special Edition instalado e rodado ao menos uma vez pela Steam** (isso cria o
  prefixo `~/.local/share/Steam/steamapps/compatdata/489830/pfx`). Confira:
  `ls ~/.local/share/Steam/steamapps/compatdata/489830/pfx/drive_c/users/`. Se o jogo está
  em outro disco, o `compatdata` fica na mesma biblioteca do jogo.
- [x] **SKSE64 instalado na pasta do jogo**: baixe de <https://skse.silverlock.org>, e copie
  `skse64_loader.exe`, `skse64_*.dll` e a pasta `Data/Scripts` do SKSE para a pasta do
  Skyrim SE (os arquivos `.exe`/`.dll` ficam **ao lado do `SkyrimSE.exe`**, os scripts em
  `Data/Scripts`). Se a versão do SKSE não corresponde à versão do jogo, o loader mostra um
  erro; use a build certa.
- [x] **Dois mods de teste** (instalados depois, no item 1): um com um `.esp` simples (por
  exemplo `TesteVFS.esp`, que apareça na lista de mods ou "Creations" do menu principal, ou um
  plugin que registre algo no console) e um com um **arquivo marcador**
  (`Data/Textures/teste/marcador.txt`).
- [x] **SSEEdit** (`SSEEdit.exe`, de <https://github.com/TES5Edit/TES5Edit/releases>)
  baixado e extraído numa pasta fora do jogo (ex.: `~/Ferramentas/SSEEdit`).
- [x] **MO2 compilado** na distrobox: `distrobox enter mo2-run -- bash -lc 'cd <repo> &&
  ./build.sh'`; o executável fica em `install/bin/ModOrganizer`, com o `mo2-vfs` ao lado.
  O branch `fase-3-steam-proton` ainda não foi enviado ao GitHub, então o pacote é esta
  compilação local; só depois de um run Linux verde existe o artefato `mo2-linux-install`.
- [x] `umu-run` existe no host: `command -v umu-run` (fora do box).
- [x] Rodar o MO2 e guardar a saída do terminal:

```sh
distrobox enter mo2-run -- <repo>/install/bin/ModOrganizer 2>&1 | tee ~/mo2-terminal.log
```

Se a janela não abrir no Wayland, use `env QT_QPA_PLATFORM=xcb` (ver checklist da Fase 1).
**Nunca rode o MO2 como root.** Dentro do box o MO2 acha o `distrobox-host-exec` sozinho.

- [x] Anote o estado inicial da pasta do jogo e do prefixo (para o item 6; `<jogo>` é
  `~/.local/share/Steam/steamapps/common/Skyrim Special Edition`):

```sh
find "<jogo>/Data" -type f | sort | sha256sum > ~/skyrim-data-antes.sha
ls ~/.local/share/Steam/steamapps/compatdata/489830/pfx/drive_c/users/ > ~/skyrim-users-antes.txt
```

- [x] A janela do MO2 abre sem crash; o terminal não mostra `Segmentation fault`.

## 1. Nova instância Skyrim SE: detecção e pastas no prefixo

- [x] Criar a instância **Skyrim Special Edition** (global). Esperado: o jogo é **detectado
  automaticamente** (caminho `.../steamapps/common/Skyrim Special Edition`), sem pedir a
  pasta.
- [x] Menu de pastas da barra de ferramentas (o ícone de pasta) -> **"Open MyGames folder"**.
  Esperado: o gerenciador de arquivos abre
  `.../compatdata/489830/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition`
  (dentro do prefixo, não uma pasta XDG como `~/.local/share/...`). Como o MO2 roda no box, o
  gerenciador de arquivos pode abrir no host ou falhar sem um; nesse caso, confira os saves: a
  aba **Saves** (e os saves do perfil) lista os saves de `.../My Games/Skyrim Special
  Edition/Saves` do prefixo, e o log do MO2 não traz o aviso de falta de prefixo.
- [x] Instalar os dois mods de teste (**File -> "Install Mod..."**) e ativar os dois.
  Esperado: contador **"Active:"** sobe para 2; o plugin `.esp` aparece na aba **Plugins**.

## 2. SKSE: jogo no menu principal e mount some no fim

Rode este item com o MO2 iniciado **pelo box**, como no item 0: é o que exercita a ponte
`distrobox-host-exec` com um jogo gráfico (o ponto de controle da Task 3 já validou o SKSE
com o `mo2-vfs` rodando direto no host).

- [x] **Tools -> "Modify Executables..."** -> **"+" -> "Add from file..."** e escolher
  `skse64_loader.exe` (na pasta do jogo, ao lado do `SkyrimSE.exe`). Título **SKSE**.
  Confirme com **"OK"**. Esperado: aparece na caixa ao lado do botão **"Run"**.
- [x] Selecionar **SKSE** e clicar em **"Run"**. Esperado: o MO2 trava (UILocker) mostrando o
  programa em execução; a primeira execução pode demorar (o `umu` atualiza o runtime) e a
  janela do jogo abre.
- [x] Em outro terminal **no host** (fora do box): `findmnt | grep -i skyrim`. Esperado:
  uma linha com a `Data` do jogo, tipo `fuse.mo2-vfs`.
- [x] A janela do jogo abre e chega ao **menu principal**. Esperado: o `.esp` de teste
  aparece em "Mods"/"Creations", **ou** o console (`~`) responde a `getskseversion` com a
  versão do SKSE.
- [x] Sair **pelo menu** do jogo. Esperado: o MO2 destrava em poucos segundos (o
  `skse64_loader.exe` inicia o jogo e sai; o mount fica até o `wineserver` terminar) e
  `findmnt | grep -i skyrim` no host fica **vazio**.

## 3. SSEEdit: lê pelo VFS e grava em `overwrite/`

- [x] Adicionar `SSEEdit.exe` como executável (**"+" -> "Add from file..."**), título
  **SSEEdit**. No campo de argumentos: `-SSE` (e, se preferir, `-IKnowWhatImDoing`). Executar.
  Esperado: o xEdit abre e lista os plugins **dos mods ativos** (inclusive o `.esp` de teste).
- [x] No xEdit, alterar algo no plugin de teste (ou criar um plugin novo) e **salvar**.
  Esperado: sem erro de gravação.
- [x] Fechar o xEdit e conferir:
  `ls ~/.local/share/ModOrganizer/<instância>/overwrite/` (ou o mod escolhido em "Create files
  in mod instead of overwrite"). Esperado: o arquivo salvo está lá, e **não** na pasta `Data`
  real: `ls "<jogo>/Data" | grep -i TesteVFS` vazio (a menos que o `.esp` seja do jogo).

## 4. Proton e variáveis por executável

- [x] **GE-Proton** instalado (`~/.local/share/Steam/compatibilitytools.d/GE-Proton*`).
  Em **Modify Executables...**, selecionar **SKSE** (ou SSEEdit). No grupo **"Proton"**,
  escolher o GE-Proton na lista (por padrão, "Automatic (same as Steam)"); na linha
  **"Environment"** do mesmo grupo (dica "NAME=value, one per line"), escrever
  `DXVK_HUD=fps` (uma variável por linha). **"OK"**.
- [x] Antes de executar, **Tools -> "Settings..." -> aba "Diagnostics" -> "Log Level" ->
  "Debug"**, para o log trazer o argv e as variáveis enviados ao `spawn`.
- [x] Executar. Esperado: o contador de FPS do **DXVK** aparece no canto da janela do jogo
  (ou da ferramenta).
- [x] `~/.local/share/ModOrganizer/<instância>/logs/run-<nome do .exe>.log` (ex.:
  `run-skse64_loader.log`, `run-SSEEdit.log`) mostra a saída do **GE-Proton** (caminho/versão do Proton nas linhas
  do `umu`). O log do MO2 (`logs/mo_interface.log`, com o nível Debug) tem as linhas
  `spawn umu-run ... in '<pasta>', env ...` (argv, variáveis acrescentadas, diretório de
  trabalho, com `DXVK_HUD=fps`) e `<exe> runs with <Proton> in <compatdata>`; ao fim, no nível
  Info, `process <pid> ended with exit code <código>`.
- [x] Voltar o grupo "Proton" para "Automatic (same as Steam)" e apagar a variável (e, se quiser,
  o nível de log para "Info (recommended)").

## 5. Erros

Em cada caso, nada pode ficar montado no fim (`findmnt | grep -i skyrim` no host vazio; no
caso a, também dentro do box).

- [x] **a) `umu-run` ausente.** O `/usr/bin` do host é imutável no Bazzite, então simule
  rodando o MO2 com o helper dentro do box, que não tem `umu-run`:
  `distrobox enter mo2-run -- env MO2_VFS_BRIDGE=direct <repo>/install/bin/ModOrganizer`.
  Executar o `SkyrimSE.exe`. Esperado: erro com `Windows programs run with umu-run: install
  umu-launcher on the host.`; nada fica montado (`findmnt -n -o TARGET,FSTYPE,SOURCE |
  grep mo2-vfs` dentro do box vazio). Depois, feche e reabra o MO2 normalmente (sem a
  variável).
- [x] **b) Prefixo ausente.** Com o MO2 fechado ou parado, renomear
  `~/.local/share/Steam/steamapps/compatdata/489830` para `489830.bak`. "Run" do
  `SkyrimSE.exe`/SKSE. Esperado: erro com `has no Proton prefix yet. Run the game once from
  Steam to create it.`; nada é montado. **Desfazer o nome** em seguida.
- [x] **c) Proton inexistente.** Fechar o MO2. Em
  `~/.local/share/ModOrganizer/<instância>/ModOrganizer.ini`, na seção `[customExecutables]`,
  a linha `<n>\proton=` do SKSE (vazia ou ausente por padrão) passa a `<n>\proton=NaoExiste`. Abrir o MO2 e executar.
  Esperado: erro com `Proton "NaoExiste", set for this executable, is not installed.
  Installed: ...` listando os Protons instalados; nada é montado. Desfazer a edição.

## 6. Depois: pasta do jogo e prefixo intactos

- [x] `find "<jogo>/Data" -type f | sort | sha256sum | diff - ~/skyrim-data-antes.sha` não
  imprime nada. Esperado: nenhum arquivo dos mods ou novo na pasta `Data` real.
- [x] `ls ~/.local/share/Steam/steamapps/compatdata/489830/pfx/drive_c/users/`: contém
  `steamuser` e, novo, o link `<usuário> -> steamuser` do `umu` (esperado, spec §1.1; compare
  com `~/skyrim-users-antes.txt`). Mais nada estranho.

## 7. MO2 fechado durante o jogo

- [x] Executar **SKSE**. Com o jogo no menu, fechar o MO2 (**File -> "Exit"**, aceitando o
  aviso de processo em execução). Feche **só** por File -> Exit: Ctrl+C ou fechar o terminal
  através do `distrobox ... | tee` derruba só o cliente do podman e o MO2 continua aberto. Esperado: o **jogo continua** rodando
  e `findmnt | grep -i skyrim` (host) ainda mostra o mount.
- [x] Sair do jogo. Esperado: poucos segundos depois o mount **some** sozinho (`findmnt`
  vazio) e `pgrep mo2-vfs` (host) fica vazio.

## 8. Encerramento

- [x] `pgrep ModOrganizer` e `pgrep mo2-vfs` vazios; nenhum `crash.log` novo e não vazio em
  `~/.local/share/ModOrganizer/<instância>/logs/`.
- [x] `findmnt | grep -i skyrim` (host) vazio.

## O que enviar se falhar

1. **Qual item** falhou e o que você viu (print ajuda).
2. **Saída do terminal** (`~/mo2-terminal.log`).
3. **Logs da instância**: `~/.local/share/ModOrganizer/<instância>/logs/` (`mo_interface.log`,
   os `run-*.log` e, se existir, `crash.log`):
   ```sh
   tar czf ~/mo2-logs.tar.gz -C ~/.local/share/ModOrganizer/<instância> logs
   ```
4. O estado do mount, **no host**: `findmnt -n -o TARGET,FSTYPE,SOURCE | grep mo2-vfs`,
   `ls /run/user/$UID/mo2-vfs/` (arquivos de estado) e `ps -ef | grep -E 'mo2-vfs|wineserver|umu'`.
5. O prefixo: `ls -la ~/.local/share/Steam/steamapps/compatdata/489830/` e
   `cat ~/.local/share/Steam/steamapps/compatdata/489830/version`.
6. Versões: `umu-run --version` (host), o Proton usado, e o número do run do GitHub Actions
   / commit do MO2.
