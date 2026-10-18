# Fase 1 — Checklist manual no Bazzite

Este checklist cobre o item 2 dos critérios de pronto da Fase 1 (spec §4.7 e §4.8).
A interface do MO2 só existe em inglês: os nomes de menus e botões abaixo estão em inglês,
exatamente como aparecem na tela. Marque cada caixa; se algo falhar, veja "O que enviar se falhar" no final.

O que **não** funciona na Fase 1 (não é falha, é esperado):

- Sem VFS (Fase 2): os mods aparecem no MO2, mas o jogo não vê o resultado.
- O botão "Run" mostra um diálogo dizendo que a execução chega na Fase 3.
- LOOT indisponível (botão desabilitado). Criação de BSA indisponível (opção oculta).
- Sem atualização automática do MO2.
- Logins do Nexus e do Steam **não são gravados** no Linux ainda: ficam só em memória e
  serão pedidos de novo a cada abertura. Cofre de senhas virá numa fase posterior.
- "Meus Jogos" e AppData apontam para pastas XDG (a Fase 3 mapeia para o prefixo Proton).
- A detecção automática do Steam cobre `~/.local/share/Steam`, `~/.steam/steam`, o Steam
  Flatpak e as bibliotecas extras listadas em `libraryfolders.vdf`; fora disso, informe o
  caminho do jogo manualmente.
- Sem plugins Python e sem o tratamento de links `nxm://` (chegam na Fase 4).

## 0. Preparação

Tudo roda **dentro de um distrobox Fedora 44**, não no host (o binário usa o Qt 6 do
Fedora 44). Detalhes e variantes em [README.md](../../README.md#rodando-no-bazzite-pt-br).

```sh
# 1. imagem e container (uma vez só), a partir do repositório clonado
git clone https://github.com/FM0Ura/ModOrganizerLinux.git && cd ModOrganizerLinux
git checkout main && git pull
podman build -t mo2-linux-build -f build/Containerfile build/
distrobox create --name mo2-run --image localhost/mo2-linux-build
# 2. baixe o artefato "mo2-linux-install" (GitHub -> Actions -> workflow "Linux" ->
#    último run verde do branch main -> Artifacts), então:
mkdir -p ~/mo2-phase1 && unzip ~/Downloads/mo2-linux-install.zip -d ~/mo2-phase1
chmod +x ~/mo2-phase1/bin/ModOrganizer
# 3. rodar (guarde a saída do terminal)
distrobox enter mo2-run -- ~/mo2-phase1/bin/ModOrganizer 2>&1 | tee ~/mo2-terminal.log
```

`QT_QPA_PLATFORM` não é necessário em um desktop (só para execução sem tela). Se a janela
não aparecer no Wayland, rode com `env QT_QPA_PLATFORM=xcb`:
`distrobox enter mo2-run -- env QT_QPA_PLATFORM=xcb ~/mo2-phase1/bin/ModOrganizer`.

- [ ] A janela do MO2 abre (primeira execução: assistente de criação de instância).
  Esperado: sem crash; o terminal pode mostrar avisos do Qt, sem `Segmentation fault`.

Se algo der errado logo na abertura:

- **Nunca rode o MO2 como root nem com `sudo`.** O Qt WebEngine (a janela principal) se
  recusa a rodar como root, e uma execução como root deixa arquivos em `/tmp` que depois
  impedem a execução como usuário normal (próximo item).
- **Crash logo depois de escolher a instância** (a janela principal usa o Qt WebEngine):
  rode de novo com a aceleração de GPU desligada:
  `distrobox enter mo2-run -- env QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu ~/mo2-phase1/bin/ModOrganizer`.
  Em placa **NVIDIA**, crie o distrobox com `--nvidia` (acrescente a opção ao
  `distrobox create` do passo 1).
- **Erro "SHM error ... permission denied"** ao abrir: uma execução anterior como root/sudo
  deixou arquivos `/tmp/qipc_*` pertencentes ao root. Confira com `ls -l /tmp/qipc_*` (os do
  MO2 se chamam `qipc_sharedmemory_mo<hash>` e `qipc_systemsem_mo<hash>`), remova-os com
  `sudo rm /tmp/qipc_sharedmemory_mo* /tmp/qipc_systemsem_mo*` e rode como usuário normal.

Se a barra de menus estiver escondida, aperte **Alt** para mostrá-la.

Para o teste você precisa de: o Skyrim Special Edition instalado no Steam (ou apenas a
pasta do jogo) e três arquivos de mod pequenos: um `.7z`, um `.zip` e um `.rar`, um deles
com instalador FOMOD. Dois dos mods devem conter uma pasta `Textures` (maiúscula) e outra
`textures` (minúscula) respectivamente (ver item 5).

## 1. Criar a instância

- [ ] No assistente, escolher **"Create a global instance"** e avançar com **"Next >"**.
- [ ] Na página do jogo, escolher o botão **Skyrim Special Edition**.
  Esperado: se o jogo estiver numa biblioteca Steam reconhecida, o botão mostra o caminho
  dele; senão mostra "No installation found".
- [ ] Se aparecer a página de edição do jogo (**Steam**, **GOG**, **Epic Games**), escolher
  **Steam**. (Selecionar uma edição avança sozinho para a próxima página.)
- [ ] (Se a detecção automática falhar) clicar em **"Browse..."** e escolher a pasta do jogo.
  Esperado: aceita a pasta que contém `SkyrimSE.exe` e segue em frente.
- [ ] Avançar com **"Next >"** pelas demais páginas até o resumo e clicar em **"Finish"**.
  Esperado: a janela principal abre; a instância fica em
  `~/.local/share/ModOrganizer/<nome da instância>/`.

## 2. Criar perfil

- [ ] Menu **Tools -> "Profiles..."** (Ctrl+P) -> **"Create"**, dar o nome (ex.: `Teste`),
  depois selecionar o perfil na lista e clicar em **"Select"** (ou escolhê-lo na caixa
  **"Profile"** da janela principal).
  Esperado: o perfil aparece e fica ativo; a pasta `profiles/Teste` é criada na instância.

## 3. Instalar três mods

Use **File -> "Install Mod..."** (Ctrl+M, ou o botão na barra de ferramentas). Abre o
seletor de arquivos **"Choose Mod"**. Durante a instalação o MO2 pergunta o nome do mod
(por exemplo na janela **"Quick Install"**, campo "Name", ou, se já existir um mod com esse
nome, na janela **"Mod Name"**); aceite ou edite e confirme.

- [ ] Mod `.7z`: instala e aparece na lista de mods. Esperado: nenhum erro de extração.
- [ ] Mod `.zip`: idem.
- [ ] Mod `.rar`: idem. (A extração de RAR só é verificada manualmente; não há teste
  automatizado.)
- [ ] Um dos três com FOMOD: abre a janela **"FOMOD Installer"**; as opções funcionam, o
  botão **"Next"** avança e, na última página, vira **"Install"** e conclui. Esperado: os
  arquivos escolhidos aparecem no mod instalado.
- [ ] Ativar os três mods (caixa de seleção ao lado do nome). Esperado: o contador
  **"Active:"** acima da lista de mods sobe.

## 4. Reordenar

- [ ] Arrastar um mod para outra posição na lista (ou usar as setas de prioridade).
  Esperado: as prioridades se renumeram e a nova ordem fica visível.

## 5. Aba Data e conflitos

- [ ] Abrir a aba **"Data"**. Esperado: a árvore mostra os arquivos dos mods ativos, com
  as colunas Name / Mod / Type / Size / Date modified; a coluna **"Mod"** aponta o mod
  vencedor de cada arquivo.
- [ ] **Case-insensitive:** com um mod contendo `Textures/...` e outro `textures/...`,
  a aba Data mostra **uma única pasta** (`Textures` ou `textures`) com os arquivos dos
  dois mods mesclados, não duas pastas separadas.
- [ ] Mods com arquivos em comum mostram o ícone de conflito na lista; dando duplo clique
  no mod e abrindo a aba **"Conflicts"** da janela de informações, vê-se quem sobrescreve
  quem.

## 6. Persistência

- [ ] Fechar o MO2 (**File -> "Exit"**) e abrir de novo
  (`distrobox enter mo2-run -- ~/mo2-phase1/bin/ModOrganizer`).
  Esperado: abre direto na instância; os 3 mods, a ordem, as caixas de ativação e o
  perfil selecionado estão iguais ao que você deixou.

## 7. Run (mensagem da Fase 3)

- [ ] Selecionar um executável na caixa ao lado do botão e clicar em **"Run"**.
  Esperado: aparece um diálogo de título "Mod Organizer" com o texto:
  "Executar programas ainda não está disponível no Linux. Esta funcionalidade chega na
  Fase 3 do port." O MO2 continua aberto e responsivo (sem crash). (O texto do diálogo
  está em português; é a única parte da interface que não está em inglês.)

## 8. Menu de contexto

- [ ] Botão direito em um mod -> **"Open in Explorer"**.
  Esperado: o gerenciador de arquivos abre na pasta do mod.
  **Limitação conhecida do ambiente:** o MO2 chama `xdg-open`, e dentro do distrobox isso
  só funciona se o shim `xdg-open` do distrobox (que repassa para o host) estiver ativo.
  Se **nada abrir**, não é um crash do MO2: anote "nada abriu" (a ação não deve derrubar o
  programa) e confira que a pasta existe em
  `~/.local/share/ModOrganizer/<nome da instância>/mods/<nome do mod>` (abra-a à mão).

## 9. Encerramento

- [ ] Fechar o MO2 normalmente (**File -> "Exit"**). Esperado: o processo termina
  (`pgrep ModOrganizer` vazio) e nenhum `crash.log` novo é criado.

## O que enviar se falhar

1. **Qual item** do checklist falhou e o que você viu (print ajuda).
2. **Saída do terminal.** Se você rodou com `| tee ~/mo2-terminal.log`, envie esse arquivo.
   Senão, rode de novo com o comando do passo 3 acima e copie o arquivo `~/mo2-terminal.log`.
3. **Logs da instância**, na pasta `logs/` dela:
   `~/.local/share/ModOrganizer/<nome da instância>/logs/` — envie `mo_interface.log` e,
   se existir, `crash.log` (escrito no mesmo diretório quando o MO2 cai). Para juntar tudo:
   ```sh
   tar czf ~/mo2-logs.tar.gz -C ~/.local/share/ModOrganizer/<nome da instância> logs
   ```
4. Versão do artefato: o número do run do GitHub Actions (e o commit) de onde baixou.
5. Para o problema de detecção do Steam: o resultado de `ls ~/.local/share/Steam/steamapps/common`.
