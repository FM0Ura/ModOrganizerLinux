# Fase 2 — Checklist manual no Bazzite

Este checklist cobre o item 2 dos critérios de pronto da Fase 2 (spec da Fase 2, §10).
A interface do MO2 só existe em inglês: os nomes de menus e botões abaixo estão em inglês,
exatamente como aparecem na tela. Marque cada caixa; se algo falhar, veja "O que enviar se falhar" no final.

O que **não** funciona na Fase 2 (não é falha, é esperado):

- Só programas **Linux nativos** são executados com o VFS. Executáveis `.exe` (jogo, SKSE,
  xEdit) continuam mostrando o aviso da Fase 3. Por isso o teste usa um gerenciador de
  arquivos como "executável".
- **O mount só existe dentro do distrobox `mo2-run`**, no mesmo ambiente em que o MO2 roda.
  O host (e a Steam do host) não o vê, e o `findmnt` do host não o mostra. Rode tudo que
  olha o mount (gerenciador de arquivos, `findmnt`, `ls`) dentro do box.
- O mount existe só enquanto o programa roda: monta ao clicar em "Run" e desmonta sozinho
  quando o último programa observado termina.
- Enquanto há mount, o refresh de mods fica adiado e acontece quando o VFS desmonta.
- Valem as limitações da Fase 1 (sem LOOT, sem Python, sem `nxm://`, logins não gravados).

## 0. Preparação

Tudo roda **dentro do distrobox Fedora 44** (`mo2-run`), como na Fase 1. Detalhes em
[README.md](../../README.md#rodando-no-bazzite-pt-br).

```sh
# 1. repositório e container (se ainda não tem), a partir do branch da Fase 2
git clone https://github.com/FM0Ura/ModOrganizerLinux.git && cd ModOrganizerLinux
git checkout fase-2-vfs-fuse && git pull
podman build -t mo2-linux-build -f build/Containerfile build/
distrobox create --name mo2-run --image localhost/mo2-linux-build
# 2. baixe o artefato "mo2-linux-install" (GitHub -> Actions -> workflow "Linux" ->
#    último run verde do branch fase-2-vfs-fuse -> Artifacts), então:
mkdir -p ~/mo2-phase2 && unzip ~/Downloads/mo2-linux-install.zip -d ~/mo2-phase2
chmod +x ~/mo2-phase2/bin/ModOrganizer ~/mo2-phase2/bin/mo2-vfs
# 3. rodar (guarde a saída do terminal)
distrobox enter mo2-run -- ~/mo2-phase2/bin/ModOrganizer 2>&1 | tee ~/mo2-terminal.log
```

O `mo2-vfs` precisa ficar na mesma pasta que o `ModOrganizer` (`bin/`); alternativamente
aponte `MO2_VFS_BINARY=/caminho/mo2-vfs`. Se a janela não abrir no Wayland, use
`env QT_QPA_PLATFORM=xcb` (ver checklist da Fase 1). **Nunca rode o MO2 como root.**

Para o teste você precisa de: Skyrim Special Edition instalado no Steam (ou apenas a pasta
do jogo) e **dois mods pequenos que tenham pelo menos um arquivo em comum** (mesmo caminho
em `Data`, conteúdo diferente; por exemplo um `Textures/teste/a.txt` em cada, com texto
diferente, e um arquivo exclusivo em cada mod). O dolphin já vem no Bazzite KDE
(`/usr/bin/dolphin`); no GNOME use `/usr/bin/nautilus`. Dentro do box confira que o
gerenciador existe: `distrobox enter mo2-run -- ls /usr/bin/dolphin`. Se não existir no box,
instale-o lá (`sudo dnf install dolphin`).

Anote o estado inicial da pasta do jogo (para o item 5):

```sh
find "<pasta do jogo>/Data" -type f | sort | sha256sum > ~/skyrim-data-antes.sha
```

- [ ] A janela do MO2 abre sem crash; o terminal não mostra `Segmentation fault`.

## 1. Instância, perfil e mods

- [ ] Criar a instância **Skyrim Special Edition** (global), como nos itens 1 e 2 do checklist da Fase 1.
- [ ] Instalar os dois mods de teste (**File -> "Install Mod..."**) e ativar os dois
  (caixa ao lado do nome). Esperado: contador **"Active:"** sobe para 2.
- [ ] Ordenar de modo que o mod B tenha prioridade maior que o mod A (B abaixo de A na lista).
  Esperado: o ícone de conflito aparece nos dois; aba **Data** mostra o arquivo comum com o mod B como vencedor.

## 2. Cadastrar o gerenciador de arquivos como executável

- [ ] Menu **Tools -> "Modify Executables..."** (ou o ícone de engrenagem ao lado da caixa de
  executáveis) -> **"+" -> "Add from file..."** e escolher `/usr/bin/dolphin` (ou `nautilus`).
  Dê o título **Dolphin VFS** e, no campo de argumentos, o caminho da pasta `Data` do jogo
  (`<pasta do jogo>/Data`). Confirme com **"OK"**.
  Esperado: o executável aparece na caixa ao lado do botão **"Run"**.

## 3. Executar e navegar em `Data`

- [ ] Selecionar **Dolphin VFS** e clicar em **"Run"**.
  Esperado: o Dolphin abre; o MO2 mostra o estado de "executando" (botão de desbloqueio) e
  não mostra o aviso da Fase 3.
- [ ] No Dolphin, navegar em `Data`.
  Esperado: aparecem os arquivos **dos dois mods** e os arquivos originais do jogo juntos;
  o arquivo em comum tem o conteúdo do **mod B** (prioridade maior); a pasta `Textures` /
  `textures` aparece **uma vez só**, com tudo mesclado (lookup sem diferenciar maiúsculas/minúsculas).
- [ ] Em outro terminal **dentro do box**, o mount aparece:
  `distrobox enter mo2-run -- bash -lc 'findmnt -n -o TARGET,FSTYPE,SOURCE | grep mo2-vfs'`.
  Esperado: uma linha com a `Data` do jogo, tipo `fuse.mo2-vfs` (ou `fuse` quando feito como root) e fonte `mo2-vfs`.
- [ ] Ainda no Dolphin, criar uma pasta ou arquivo novo em `Data` (ex.: `Teste-VFS.txt`).
  Esperado: criado sem erro.

## 4. O que foi criado vai para `overwrite/`

- [ ] Fechar o Dolphin. Esperado: o MO2 volta ao normal (desbloqueado) em poucos segundos.
- [ ] Conferir: `ls ~/.local/share/ModOrganizer/<nome da instância>/overwrite/`.
  Esperado: `Teste-VFS.txt` está lá; a aba **Data** / o refresh do MO2 também o mostra
  (o refresh adiado roda ao desmontar).
- [ ] O mount sumiu: o `findmnt` do item 3 (dentro do box) não mostra mais linha.

## 5. A pasta do jogo continua intacta

- [ ] `find "<pasta do jogo>/Data" -type f | sort | sha256sum | diff - ~/skyrim-data-antes.sha`
  não imprime nada. Esperado: nenhum arquivo do jogo foi criado, apagado ou renomeado
  (arquivos dos mods e novos arquivos nunca ficam na pasta do jogo).
- [ ] No Dolphin (nova execução), apagar um arquivo **original do jogo** e renomear outro; ao
  fechar, repetir o `diff` acima. Esperado: nada mudou no disco (apagar só esconde até
  desmontar; renomear copia para `overwrite/`).

## 6. Fechar o MO2 com o gerenciador aberto

- [ ] Executar **Dolphin VFS** de novo. Com ele aberto, fechar o MO2 (**File -> "Exit"**).
  Esperado: o MO2 pergunta ou espera pelo processo; `findmnt` (dentro do box) **continua**
  mostrando a `Data` montada e o Dolphin continua navegando normalmente.
- [ ] Fechar o Dolphin.
  Esperado: a `Data` é desmontada sozinha (`findmnt` vazio) pouco depois.

## 7. `mo2-vfs` morto: reabrir limpa o mount

- [ ] Abrir o MO2 e executar **Dolphin VFS**. Em outro terminal (dentro do box):
  `pkill -9 mo2-vfs`.
  Esperado: o Dolphin pode mostrar erro de "ponto de montagem desconectado" ao navegar; o
  MO2 avisa que o virtual file system se perdeu (ao fim da execução) e não cai.
- [ ] Fechar o Dolphin e o MO2; conferir no box: `findmnt -n -o TARGET,FSTYPE,SOURCE | grep mo2-vfs`.
  Esperado: pode ainda haver um mount órfão (é o caso do teste).
- [ ] Abrir o MO2 de novo.
  Esperado: abre sem erro e o mount órfão é limpo (`findmnt` vazio dentro do box; a `Data`
  do jogo volta a listar só os arquivos originais).

## 8. Encerramento

- [ ] Fechar o MO2 (**File -> "Exit"**). `pgrep ModOrganizer` e `pgrep mo2-vfs` vazios; nenhum `crash.log` novo.
- [ ] No fim, dentro do box:
  `findmnt -n -o TARGET,FSTYPE,SOURCE | grep mo2-vfs` não imprime nada.

## O que enviar se falhar

1. **Qual item** falhou e o que você viu (print ajuda).
2. **Saída do terminal** (`~/mo2-terminal.log`, do `tee` do passo 3 da preparação).
3. **Logs da instância**: `~/.local/share/ModOrganizer/<nome da instância>/logs/`
   (`mo_interface.log` e, se existir, `crash.log`):
   ```sh
   tar czf ~/mo2-logs.tar.gz -C ~/.local/share/ModOrganizer/<nome da instância> logs
   ```
4. O estado do mount, dentro do box: `findmnt -n -o TARGET,FSTYPE,SOURCE | grep mo2-vfs`
   e `ls /run/user/$UID/mo2-vfs/` (arquivos de estado).
5. Versão do artefato: número do run do GitHub Actions e o commit de onde baixou.
