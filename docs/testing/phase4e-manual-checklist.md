# Fase 4e — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-24-phase4e-bsa-design.md`](../superpowers/specs/2026-10-24-phase4e-bsa-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`. Instância de Skyrim SE das fases anteriores. Box com o pacote `tbb` (o `libbsa_packer.so` usa o `libtbb`; README, "Rodando no Bazzite"); para compilar no box, `gsl-lite-devel tbb-devel` (README, "Building").

**Build:** o `install/` do worktree `ModOrganizerLinux-4e` (`./build.sh` na distrobox `mo2-run`) ou, depois de um run Linux verde do PR, o artefato `mo2-linux-install` desse run (restaure a permissão de execução de `bin/ModOrganizer` e `bin/mo2-vfs`).

## Preparação

- [ ] Um mod de teste com arquivos soltos: por exemplo, instale o `MO2Teste-Imagens.7z` (`~/Documentos/mo2-testes-fase4/arquivos/`) e acrescente à pasta dele uma textura e um mesh de verdade de outro mod (copiados, não movidos), ou use um mod pequeno do Nexus com texturas soltas. Anote a lista de arquivos (`find <pasta do mod> -type f`).
- [ ] Em **Configurações → Plugins → BSA Packer**, ligue `create_plugins` e `hide_loose_assets`.

## 1. Plugin carregado

- [ ] **Configurações → Plugins** mostra "BSA Packer"; **Ferramentas** tem **BSA Packer**. No log (`logs/mo_interface.log`), nenhum erro ao carregar `libbsa_packer.so`.

## 2. Empacotar

- [ ] **Ferramentas → BSA Packer**, escolher o mod de teste, OK. Esperado: a janela "Writing Archive" e depois "Created archive(s): <mod>.bsa"; na pasta do mod: `<mod>.bsa` e `<mod>.esp` (plugin vazio); as pastas soltas renomeadas para `<pasta>.mohidden` (menos os arquivos da lista negra, como `.txt`, que voltam ao lugar).
- [ ] Aba **Archives**: o `<mod>.bsa` aparece; aba **Data**: os arquivos do mod aparecem vindos do BSA; pré-visualizar um `.dds` e um `.txt`/imagem de dentro do BSA (pré-visualização da 4c.1/4d.1) mostra o conteúdo.
- [ ] BSA Extractor (4c.1): numa cópia do mod (ou depois de reinstalar o mod de teste), extrair o `<mod>.bsa` criado; os arquivos extraídos são iguais aos originais (`cmp` com os da lista anotada).

## 3. No jogo

- [ ] Ativar o `<mod>.esp` na aba de plugins e executar o Skyrim SE (ou SKSE) pelo MO2. Esperado: o jogo abre e carrega um save sem queda; com uma textura trocada de verdade no mod, ela aparece no jogo (vinda do BSA, já que os arquivos soltos estão `.mohidden`).

## 4. Erros

- [ ] Rodar o BSA Packer num mod cujos arquivos estão todos na lista negra (por exemplo, só `.txt`). Esperado: nenhum `.bsa` criado, nenhuma queda, e o MO2 não lista nenhum arquivo como criado. Esse caso só aparece no log em nível de depuração (`no files to pack into <caminho>`); não há linha `failed to write archive` para ele (essa linha é de falhas de gravação, como disco cheio ou pasta sem permissão).

## 5. Regressão

- [ ] SKSE inicia o jogo com os mods (Fase 3); LOOT ordena (4b); o BSA Extractor e a pré-visualização de BSA (4c.1) funcionam num BSA de outro mod.
- [ ] Fechar o MO2 pelo menu (**Arquivo → Sair**): fecha sem travar e o `logs/crash.log` continua vazio.

**Ao terminar:** registre aqui o resultado de cada item e, em "Observações", o que precisou de correção. Para voltar o mod de teste ao estado anterior: apagar o `.bsa` e o `.esp` criados e tirar o `.mohidden` das pastas.

## Observações
