# Fase 2 — Checklist manual no Bazzite

Este checklist cobre o item 2 dos critérios de pronto da Fase 2 (spec da Fase 2, §10).
A interface do MO2 só existe em inglês: os nomes de menus e botões abaixo estão em inglês,
exatamente como aparecem na tela. Marque cada caixa; se algo falhar, veja "O que enviar se falhar" no final.

O que **não** funciona na Fase 2 (não é falha, é esperado):

- Só programas **Linux nativos** são executados com o VFS. Executáveis `.exe` (jogo, SKSE,
  xEdit) continuam mostrando o aviso da Fase 3. Por isso o teste usa um programa Linux
  simples como "executável" (um terminal com um script, ou um gerenciador de arquivos).
- **O mount só existe dentro do distrobox `mo2-run`**, no mesmo ambiente em que o MO2 roda.
  O host (e a Steam do host) não o vê, e o `findmnt` do host não o mostra. Rode tudo que
  olha o mount (gerenciador de arquivos, `findmnt`, `ls`) dentro do box.
- O mount existe só enquanto o programa roda: monta ao clicar em "Run" e desmonta sozinho
  quando o último programa observado termina.
- **Programas que entregam o trabalho a outra instância já aberta** (o Dolphin/Nautilus do
  host ou de outra janela, pela sessão D-Bus compartilhada entre host e box) **ou que chamam
  `setsid`** saem do grupo de processos observado: o processo iniciado pelo MO2 termina na
  hora e o mount acaba antes do esperado (a janela que aparece não está vendo o VFS). Por
  isso o teste principal usa um terminal com um script; o Dolphin só com `--new-window` e
  com o Dolphin do host fechado.
- Enquanto há mount, o refresh de mods fica adiado e acontece quando o VFS desmonta.
- Valem as limitações da Fase 1 (sem LOOT, sem Python, sem `nxm://`, logins não gravados).

## 0. Preparação

Tudo roda **dentro do distrobox Fedora 44** (`mo2-run`), como na Fase 1. Detalhes em
[README.md](../../README.md#rodando-no-bazzite-pt-br).

```sh
# 1. repositório e container (se ainda não tem), a partir do branch da Fase 2
git clone https://github.com/FM0Ura/ModOrganizerLinux.git && cd ModOrganizerLinux
git checkout main && git pull
podman build -t mo2-linux-build -f build/Containerfile build/
distrobox create --name mo2-run --image localhost/mo2-linux-build
# 2. baixe o artefato "mo2-linux-install" (GitHub -> Actions -> workflow "Linux" ->
#    último run verde do branch main -> Artifacts), então:
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
diferente, e um arquivo exclusivo em cada mod).

O programa de teste principal é um **terminal dentro do box com um script** que lista a
pasta e espera você apertar Enter: ele fica no grupo de processos que o MO2 observa, então
o mount dura até você apertar Enter. Crie o script (a pasta `~` é a mesma no host e no box):

```sh
mkdir -p ~/bin && cat > ~/bin/vfs-shell.sh <<'EOF2'
#!/bin/bash
# abre um shell na pasta recebida; o mount dura até este shell terminar
cd "$1" || exit 1
ls -la
echo "Mount ativo em $PWD. Use ls/cat/touch aqui; digite exit (ou Ctrl+D) para sair."
exec bash -i
EOF2
chmod +x ~/bin/vfs-shell.sh
```

E confira, dentro do box, que existe um terminal: `distrobox enter mo2-run -- bash -lc
'command -v konsole xterm'`. Se não houver nenhum, instale um lá
(`distrobox enter mo2-run -- sudo dnf install -y xterm`).

Alternativa (gerenciador de arquivos): o dolphin (`/usr/bin/dolphin`, Bazzite KDE; no GNOME
`/usr/bin/nautilus`) também precisa existir **dentro do box** (`sudo dnf install dolphin`).
Use sempre `--new-window` e **feche antes todas as janelas do Dolphin do host**: senão o
Dolphin iniciado pelo MO2 entrega a pasta à instância que já está aberta (fora do box e do
grupo observado), sai na hora e o `mo2-vfs` desmonta.

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

## 2. Cadastrar o programa de teste como executável

Nos itens abaixo, **"o programa de teste"** é o **Shell VFS** (principal) ou, se preferir, o
**Dolphin VFS** (alternativa). "Fechar o programa de teste" é digitar `exit` no terminal do
Shell VFS (ou fechar a janela do Dolphin).

- [ ] Menu **Tools -> "Modify Executables..."** (ou o ícone de engrenagem ao lado da caixa de
  executáveis) -> **"+" -> "Add from file..."** e escolher o terminal do box
  (`/usr/bin/xterm` ou `/usr/bin/konsole`). Dê o título **Shell VFS** e, no campo de
  argumentos, o caminho completo do script e a pasta `Data` do jogo:
  xterm: `-e /home/<usuário>/bin/vfs-shell.sh "<pasta do jogo>/Data"`;
  konsole: `--nofork -e /home/<usuário>/bin/vfs-shell.sh "<pasta do jogo>/Data"` (sem
  `--nofork` o Konsole pode entregar a janela a uma instância já aberta). Confirme com **"OK"**.
  Esperado: o executável aparece na caixa ao lado do botão **"Run"**.
- [ ] (Alternativa) Da mesma forma, `/usr/bin/dolphin` com o título **Dolphin VFS** e os
  argumentos `--new-window "<pasta do jogo>/Data"`. Antes de cada execução, feche todas as
  janelas do Dolphin do host.

## 3. Executar e navegar em `Data`

- [ ] Selecionar o programa de teste e clicar em **"Run"**.
  Esperado: o terminal (ou o Dolphin) abre e continua aberto; o MO2 mostra o estado de
  "executando" (botão de desbloqueio) e não mostra o aviso da Fase 3. Se a janela fechar
  sozinha na hora e o MO2 já voltar ao normal, o programa saiu do grupo observado (veja o
  aviso no topo).
- [ ] No programa de teste, navegar em `Data` (`ls`, `cat Textures/teste/a.txt`).
  Esperado: aparecem os arquivos **dos dois mods** e os arquivos originais do jogo juntos;
  o arquivo em comum tem o conteúdo do **mod B** (prioridade maior); a pasta `Textures` /
  `textures` aparece **uma vez só**, com tudo mesclado (lookup sem diferenciar maiúsculas/minúsculas).
- [ ] Em outro terminal **dentro do box**, o mount aparece:
  `distrobox enter mo2-run -- bash -lc 'findmnt -n -o TARGET,FSTYPE,SOURCE | grep mo2-vfs'`.
  Esperado: uma linha com a `Data` do jogo, tipo `fuse.mo2-vfs` (ou `fuse` quando feito como root) e fonte `mo2-vfs`.
- [ ] Ainda no programa de teste, criar uma pasta ou arquivo novo em `Data` (ex.:
  `touch Teste-VFS.txt`).
  Esperado: criado sem erro.

## 4. O que foi criado vai para `overwrite/`

- [ ] Fechar o programa de teste. Esperado: o MO2 volta ao normal (desbloqueado) em poucos segundos.
- [ ] Conferir: `ls ~/.local/share/ModOrganizer/<nome da instância>/overwrite/`.
  Esperado: `Teste-VFS.txt` está lá; a aba **Data** / o refresh do MO2 também o mostra
  (o refresh adiado roda ao desmontar).
- [ ] O mount sumiu: o `findmnt` do item 3 (dentro do box) não mostra mais linha.

## 5. A pasta do jogo continua intacta

- [ ] `find "<pasta do jogo>/Data" -type f | sort | sha256sum | diff - ~/skyrim-data-antes.sha`
  não imprime nada. Esperado: nenhum arquivo do jogo foi criado, apagado ou renomeado
  (arquivos dos mods e novos arquivos nunca ficam na pasta do jogo).
- [ ] No programa de teste (nova execução), apagar um arquivo **original do jogo** e
  renomear outro (`rm`, `mv`); ao fechar, repetir o `diff` acima. Esperado: nada mudou no disco (apagar só esconde até
  desmontar; renomear copia para `overwrite/`).

## 6. Fechar o MO2 com o programa de teste aberto

- [ ] Executar o programa de teste de novo. Com ele aberto, fechar o MO2 (**File -> "Exit"**).
  Esperado: o MO2 pergunta ou espera pelo processo; `findmnt` (dentro do box) **continua**
  mostrando a `Data` montada e o programa de teste continua vendo os arquivos dos mods.
- [ ] Repetir com o MO2 aberto pelo terminal: com o programa de teste aberto, apertar
  **Ctrl+C** no terminal do MO2 (ou fechar esse terminal).
  Esperado: o mount **continua** (`findmnt` dentro do box) e o programa de teste continua
  vendo os arquivos dos mods, feche o MO2 ou não: o `mo2-vfs` roda numa sessão própria e
  não recebe o Ctrl+C nem o fechamento do terminal.
- [ ] Fechar o programa de teste.
  Esperado: a `Data` é desmontada sozinha (`findmnt` vazio) pouco depois.

## 7. `mo2-vfs` morto: reabrir limpa o mount

- [ ] Abrir o MO2 e executar o programa de teste. Em outro terminal (dentro do box):
  `pkill -9 mo2-vfs`.
  Esperado: o MO2 limpa o mount morto logo em seguida (`findmnt -n -o TARGET,FSTYPE,SOURCE |
  grep mo2-vfs` vazio dentro do box; `ls "<pasta do jogo>/Data"` em outro terminal lista
  só os arquivos originais). O programa de teste perde os arquivos dos mods (no terminal,
  `ls` na pasta atual pode dar erro: `cd "<pasta do jogo>/Data"` de novo mostra os originais).
- [ ] Fechar o programa de teste.
  Esperado: o MO2 avisa que o virtual file system se perdeu (ao fim da execução) e não cai;
  a lista de plugins (aba **Plugins**) continua com `Skyrim.esm` e os demais plugins do jogo.
- [ ] Fechar e abrir o MO2 de novo.
  Esperado: abre sem erro e sem aviso de mount órfão.

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
