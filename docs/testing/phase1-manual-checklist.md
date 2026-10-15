# Fase 1 — Checklist manual no Bazzite

Este checklist cobre o item 2 dos critérios de pronto da Fase 1 (spec §4.7 e §4.8).
Marque cada caixa; se algo falhar, veja "O que enviar se falhar" no final.

O que **não** funciona na Fase 1 (não é falha, é esperado):

- Sem VFS (Fase 2): os mods aparecem no MO2, mas o jogo não vê o resultado.
- "Executar" / "Run" mostra um diálogo dizendo que a execução chega na Fase 3.
- LOOT indisponível (botão desabilitado). Criação de BSA indisponível (opção oculta).
- Sem atualização automática do MO2.
- Logins do Nexus e do Steam **não são gravados** no Linux ainda: ficam só em memória e
  serão pedidos de novo a cada abertura. Cofre de senhas virá numa fase posterior.
- "Meus Jogos" e AppData apontam para pastas XDG (a Fase 3 mapeia para o prefixo Proton).
- A detecção automática do Steam cobre a biblioteca padrão (`~/.local/share/Steam`);
  para outra biblioteca informe o caminho manualmente.

## 0. Preparação

Tudo roda **dentro de um distrobox Fedora 44**, não no host (o binário usa o Qt 6 do
Fedora 44). Detalhes e variantes em [README.md](../../README.md#rodando-no-bazzite-pt-br).

```sh
# 1. imagem e container (uma vez só), a partir do repositório clonado
git clone https://github.com/FM0Ura/ModOrganizerLinux.git && cd ModOrganizerLinux
git checkout claude/serene-maxwell-1dyrbg
podman build -t mo2-linux-build -f build/Containerfile build/
distrobox create --name mo2-run --image localhost/mo2-linux-build
# 2. baixe o artefato "mo2-linux-install" (GitHub -> Actions -> workflow "Linux" ->
#    último run verde da branch claude/serene-maxwell-1dyrbg -> Artifacts), então:
mkdir -p ~/mo2-phase1 && unzip ~/Downloads/mo2-linux-install.zip -d ~/mo2-phase1
chmod +x ~/mo2-phase1/bin/ModOrganizer
# 3. rodar
distrobox enter mo2-run -- env -u QT_QPA_PLATFORM ~/mo2-phase1/bin/ModOrganizer 2>&1 | tee ~/mo2-terminal.log
```

O `env -u QT_QPA_PLATFORM` é necessário porque a imagem do `Containerfile` define
`QT_QPA_PLATFORM=offscreen` (para os testes sem tela); com ele a janela nunca apareceria.

- [ ] A janela do MO2 abre (primeira execução: diálogo de criação/seleção de instância).
  Esperado: sem crash; o terminal pode mostrar avisos do Qt, sem `Segmentation fault`.
  `QT_QPA_PLATFORM` não deve estar definido (só serve para execução sem tela).

Para o teste você precisa de: o Skyrim Special Edition instalado no Steam (ou apenas a
pasta do jogo) e três arquivos de mod pequenos: um `.7z`, um `.zip` e um `.rar`, um deles
com instalador FOMOD. Dois dos mods devem conter uma pasta `Textures` (maiúscula) e outra
`textures` (minúscula) respectivamente (ver item 5).

## 1. Criar a instância

- [ ] Criar nova instância global, tipo Skyrim Special Edition.
  Esperado: o MO2 detecta o jogo automaticamente se estiver na biblioteca Steam padrão.
- [ ] (Se a detecção automática falhar) escolher o caminho do jogo manualmente.
  Esperado: aceita a pasta que contém `SkyrimSE.exe`; segue até o fim do assistente.
- [ ] Concluir o assistente. Esperado: a janela principal abre; a instância fica em
  `~/.local/share/ModOrganizer/<nome da instância>/`.

## 2. Criar perfil

- [ ] Menu de perfis -> criar um segundo perfil (ex.: `Teste`) e selecioná-lo.
  Esperado: o perfil aparece e fica ativo; a pasta `profiles/Teste` é criada na instância.

## 3. Instalar três mods

Use o botão "Instalar mod a partir de arquivo" (ou arraste o arquivo para a lista de mods).

- [ ] Mod `.7z`: instala e aparece na lista de mods. Esperado: nenhum erro de extração.
- [ ] Mod `.zip`: idem.
- [ ] Mod `.rar`: idem. (A extração de RAR só é verificada manualmente; não há teste
  automatizado.)
- [ ] Um dos três com FOMOD: o instalador FOMOD abre, as opções funcionam, o botão
  "Instalar" conclui. Esperado: os arquivos escolhidos aparecem no mod instalado.
- [ ] Ativar os três mods (caixa de seleção). Esperado: o contador de mods ativos sobe.

## 4. Reordenar

- [ ] Arrastar um mod para outra posição na lista (ou usar as setas de prioridade).
  Esperado: as prioridades se renumeram e a nova ordem fica visível.

## 5. Aba Data e conflitos

- [ ] Abrir a aba **Data**. Esperado: a árvore mostra os arquivos dos mods ativos,
  com a coluna "Origem" apontando o mod vencedor.
- [ ] **Case-insensitive:** com um mod contendo `Textures/...` e outro `textures/...`,
  a aba Data mostra **uma única pasta** `Textures`/`textures` com os arquivos dos dois
  mods mesclados (não duas pastas separadas).
- [ ] Mods com arquivos em comum mostram o ícone de conflito na lista; o detalhe do mod
  (duplo clique -> aba Conflitos) lista quem sobrescreve quem.

## 6. Persistência

- [ ] Fechar o MO2 (menu Arquivo -> Sair) e abrir de novo
  (`distrobox enter mo2-run -- env -u QT_QPA_PLATFORM ~/mo2-phase1/bin/ModOrganizer`).
  Esperado: abre direto na instância; os 3 mods, a ordem, as caixas de ativação e o
  perfil selecionado estão iguais ao que você deixou.

## 7. Executar (mensagem da Fase 3)

- [ ] Selecionar um executável (ex.: SKSE/Skyrim SE) e clicar em **Executar** / **Run**.
  Esperado: aparece um diálogo informando que a execução de programas chega na Fase 3;
  o MO2 continua aberto e responsivo (sem crash).

## 8. Menu de contexto

- [ ] Botão direito em um mod -> "Abrir pasta" / "Open in Explorer".
  Esperado: o gerenciador de arquivos do Bazzite abre na pasta do mod
  (`.../mods/<nome do mod>`).

## 9. Encerramento

- [ ] Fechar o MO2 normalmente. Esperado: o processo termina (`pgrep ModOrganizer` vazio)
  e não deixa `crash.log` novo.

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
