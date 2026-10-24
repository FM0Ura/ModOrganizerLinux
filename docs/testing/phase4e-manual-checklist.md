# Fase 4e — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-24-phase4e-bsa-design.md`](../superpowers/specs/2026-10-24-phase4e-bsa-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`. Instância de Skyrim SE das fases anteriores. Box com o pacote `tbb` (o `libbsa_packer.so` usa o `libtbb`; README, "Rodando no Bazzite"); para compilar no box, `gsl-lite-devel tbb-devel openssl-devel` (README, "Building").

**Build:** o `install/` do worktree `ModOrganizerLinux-4e` (`./build.sh` na distrobox `mo2-run`) ou, depois de um run Linux verde do PR, o artefato `mo2-linux-install` desse run (restaure a permissão de execução de `bin/ModOrganizer` e `bin/mo2-vfs`).

## Preparação

- [x] Um mod de teste com arquivos soltos: por exemplo, instale o `MO2Teste-Imagens.7z` (`~/Documentos/mo2-testes-fase4/arquivos/`) e acrescente à pasta dele uma textura e um mesh de verdade de outro mod (copiados, não movidos), ou use um mod pequeno do Nexus com texturas soltas. Deixe a extensão de uma das texturas em maiúsculas (`.DDS`), para conferir no item 3 que ela também é achada dentro do BSA. Anote a lista de arquivos (`find <pasta do mod> -type f`). (Resultado: OK, 2026-10-24: `MO2Teste-BSAPacker.zip` de `~/Documentos/mo2-testes-fase4/arquivos/`, instalado como `MOeasd`; a textura visível é o `SweetRoll01.DDS`.)
- [x] Em **Configurações → Plugins → BSA Packer**, ligue `create_plugins` e `hide_loose_assets`. (Resultado: OK, 2026-10-24.)

## 1. Plugin carregado

- [x] **Configurações → Plugins** mostra "BSA Packer"; **Ferramentas** tem **BSA Packer**. No log (`logs/mo_interface.log`), nenhum erro ao carregar `libbsa_packer.so`. (Resultado: OK, 2026-10-24: a ferramenta foi usada pelo menu e o log não tem erro do `libbsa_packer.so`.)

## 2. Empacotar

- [x] **Ferramentas → BSA Packer**. No diálogo "BSA Packer", em "Choose a mod to pack:" escolher o mod de teste; em "Choose a name for the packed archive:" a lista traz os plugins que o mod já tem (com a extensão trocada por `.bsa`) e, no fim, `<new filename>.bsa`: o OK só habilita depois de selecionar um item da lista. Selecionar `<new filename>.bsa` e OK; o prompt "Archive name (no file extension):" vem com o nome do mod; confirmar com o nome padrão (`<mod>`) ou escolher outro (`<nome>` abaixo). Esperado: a janela "Writing Archive" e depois "Created archive(s):" com `<nome>.bsa`; na pasta do mod: `<nome>.bsa` e `<nome>.esp` (plugin vazio); as pastas soltas renomeadas para `<pasta>.mohidden`. Os arquivos com as extensões de `blacklisted_files` (como `.txt`) voltam soltos ao lugar, mas também estão dentro do `<nome>.bsa`: com o valor padrão a lista negra não é aplicada ao arquivo (defeito do upstream, em toda plataforma; README, "Criação de BSA/BA2"). Um arquivo sem extensão (por exemplo `LICENSE` numa subpasta) não entra no `.bsa`, com um aviso `… is not packed into …: it has no extension` no log. (Resultado: OK, 2026-10-24: `MOeasd.bsa` (v105, 6 arquivos, nomes em minúsculas) e `MOeasd.esp` criados; `meshes` e `textures` viraram `.mohidden`.)
- [x] Apertar **F5** (**Refresh**): o MO2 não atualiza sozinho depois do BSA Packer. Só então conferir as abas e a lista de plugins abaixo e no item 3. (Resultado: OK, 2026-10-24.)
- [x] Aba **Archives**: o `<nome>.bsa` aparece; aba **Data**: os arquivos do mod aparecem vindos do BSA; pré-visualizar um `.dds` e um `.txt`/imagem de dentro do BSA (pré-visualização da 4c.1/4d.1) mostra o conteúdo. (Resultado: OK, 2026-10-24.)
- [x] BSA Extractor (4c.1): numa cópia do mod (ou depois de reinstalar o mod de teste), extrair o `<nome>.bsa` criado; os arquivos extraídos são iguais aos originais (`cmp` com os da lista anotada). (Resultado: OK, 2026-10-24: o `MOeasd.bsa`, instalado de novo como o mod `MOextrair` (`MO2Teste-BSAExtrair.zip`), foi extraído pelo BSA Extractor; os 6 arquivos são iguais aos originais (`cmp`), com os nomes em minúsculas.)

## 3. No jogo

- [x] Ativar o `<nome>.esp` na aba de plugins e executar o Skyrim SE (ou SKSE) pelo MO2. Esperado: o jogo abre e carrega um save sem queda; com uma textura trocada de verdade no mod, ela aparece no jogo (vinda do BSA, já que os arquivos soltos estão `.mohidden`). (Resultado: OK, 2026-10-24: com os arquivos soltos `.mohidden`, o sweet roll (`player.placeatme 64B3D`) aparece com a textura magenta e verde vinda do BSA.)

## 4. Erros

- [x] Criar um mod vazio (na lista de mods, botão direito → **Create empty mod above**). O MO2 marca um mod sem arquivos como inválido, e o diálogo do BSA Packer só lista os mods válidos: antes de empacotar, botão direito no mod vazio → **Ignore missing data**. Rodar o BSA Packer e escolher o mod vazio. Esperado: nenhum `.bsa` criado, nenhuma queda, e o MO2 não lista nenhum arquivo como criado. Esse caso só aparece no log em nível de depuração (`no files to pack into <caminho>`); não há linha `failed to write archive` para ele (essa linha é de falhas de gravação, como disco cheio ou pasta sem permissão). Com `create_plugins` e `hide_loose_assets` ligados, o `.esp` vazio ainda é criado mesmo sem nenhum arquivo criado (é o comportamento do upstream, não um defeito): apague-o, ou apague o mod vazio. (Resultado: OK, 2026-10-24: sem **Ignore missing data** o mod vazio nem aparece no diálogo; com ele, nenhum `.bsa` criado e nenhuma queda.)

## 5. Regressão

- [x] SKSE inicia o jogo com os mods (Fase 3); LOOT ordena (4b); o BSA Extractor e a pré-visualização de BSA (4c.1) funcionam num BSA de outro mod. (Resultado: OK, 2026-10-24.)
- [x] Fechar o MO2 pelo menu (**Arquivo → Sair**): fecha sem travar e o `logs/crash.log` continua vazio. (Resultado: OK, 2026-10-24: fechou sem travar e o `crash.log` não mudou; ele já tinha entradas de antes, ver Observações.)

**Ao terminar:** registre aqui o resultado de cada item e, em "Observações", o que precisou de correção. Para voltar o mod de teste ao estado anterior: apagar o `.bsa` e o `.esp` criados e tirar o `.mohidden` das pastas.

## Observações

- Todos os itens aprovados (2026-10-24).
- O `textures/mo2teste/leia.txt` entrou no BSA e também ficou solto fora do `.mohidden`: é o defeito do upstream do `blacklisted_files` (o valor padrão não é aplicado; README, "Criação de BSA/BA2"), não uma falha do Linux.
- O primeiro alvo visível (`textures/landscape/dirt01.DDS`) não aparecia onde o usuário estava; trocado pela textura do sweet roll, que se cria com o console.
- O `crash.log` da instância já tinha entradas de quedas anteriores no mesmo dia, fora deste checklist (SIGSEGV em `OzoneUtilQt::getQOpenGLContext`, do QtWebEngine, durante o `ExitModOrganizer` ao trocar ou criar instância; uma também em `TutorialControl::lockUI`): registradas como pendência à parte.
