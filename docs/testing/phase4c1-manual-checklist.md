# Fase 4c.1 — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-20-phase4c1-cpp-plugins-design.md`](../superpowers/specs/2026-10-20-phase4c1-cpp-plugins-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 compilado na distrobox `mo2-run` (`./build.sh`, resultado em `install/`) e iniciado **pelo box**: `distrobox enter mo2-run -- <repo>/install/bin/ModOrganizer`. Instância de Skyrim SE das fases anteriores.

**Antes de começar:** os itens 1 e 2 pedem um arquivo BAIN e um "bundle" (arquivo com outro arquivo dentro); o item 4 pede um mod com `.bsa`; o item 7 só vale para quem usa o FNIS no Skyrim (LE). Os itens 8 a 10 usam o perfil da instância; o item 8 restaura os INIs do perfil que as versões anteriores criaram vazios (o log do MO2 registra cada um), e os INIs reais do prefixo não são alterados.

| # | Passo | Esperado | Resultado |
|---|---|---|---|
| 1 | Instalar um mod BAIN (pacote com `00 Core`/`10 …`). | O diálogo BAIN aparece; as opções escolhidas viram os arquivos do mod. | OK |
| 2 | Instalar um "bundle" (arquivo com outro arquivo dentro). | O MO2 oferece o arquivo interno. | OK |
| 3 | Pela aba Data, pré-visualizar um `.txt` ou `.ini` e uma imagem (`.png`/`.jpg`; `.dds` não: o `preview_base` deixa o DDS para o plugin de pré-visualização DDS, em Python, Fase 4d); depois ver o conteúdo de um `.bsa` pela pré-visualização. | O texto, a imagem e os arquivos do BSA aparecem na janela de pré-visualização; os do BSA em árvore de pastas (`meshes/…`, `textures/…`), não em lista plana. | OK |
| 4 | Configurações → Plugins → BSA Extractor: desligar "only trigger bsa extraction for alternate game sources" (padrão: ligado, e então a pergunta não aparece para um mod comum). Instalar um mod com `.bsa` e responder Sim em "Extract BSA". | Os arquivos do BSA são extraídos para a pasta do mod (o MO2 ainda pergunta se remove os BSA extraídos). | OK |
| 5 | Deixar arquivos no `overwrite`. Abrir as configurações do plugin `diagnose_basic` (Configurações → Plugins). | Aviso no ícone de problemas; não há opção de atributos de arquivo (`check_fileattributes`) nas configurações do plugin. | OK |
| 6 | Ferramentas → INI Editor: editar e salvar um INI do perfil. Depois, em Configurações → Plugins → INI Editor, ligar "Use an external editor to open the files" (a opção seguinte, "associated", não tem efeito no Linux) e abrir o INI Editor de novo. | O INI é salvo; no modo externo o INI abre no editor do host (`xdg-open`). | OK |
| 7 | Só no Skyrim (LE) e para quem usa FNIS: o `check_fnis` exige o jogo "Skyrim" (`gameDependency("Skyrim")`, como no upstream) e fica inativo no Skyrim SE. FNIS instalado como mod, com `tools/GenerateFNIS_for_Users/GenerateFNISforUsers.exe` no Data virtual): instalar, ativar ou desativar um mod de animação com arquivos `FNIS_*_List.txt`, `FNIS*Behavior.txt`, `PatchList.txt` ou `skeleton*.hkx`, e executar o jogo (SKSE). | Pergunta "Run FNIS before …?" ("FNIS source data has been changed") e, respondendo Sim, o `GenerateFNISforUsers.exe` roda pelo Proton com o VFS. | N/A (Skyrim SE) |
| 8 | INI Bakery, com INIs por perfil ligados (Perfis → "Use profile-specific Game INI Files"): abrir a instância e conferir `skyrim.ini` e `skyrimprefs.ini` na pasta do perfil (`profiles/<perfil>/`). Anotar data e tamanho de `Skyrim.ini` e `SkyrimPrefs.ini` do prefixo (`…/compatdata/489830/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/`). Pelo INI Editor, mudar um valor visível em `skyrimprefs.ini` (ex.: em `[Display]`, `bFull Screen=0` e `iSize W`/`iSize H` para uma janela menor), salvar, executar o jogo pelo MO2 e sair. | Os INIs do perfil não estão vazios: são cópias dos do prefixo (o `skyrim.ini` mantém `[Launcher]` `bEnableFileSelection=1`), e o log do MO2 tem `restored the empty profile INI` para cada um restaurado. O jogo abre com o valor mudado. Depois, `Skyrim.ini` e `SkyrimPrefs.ini` do prefixo continuam com a data e o tamanho anotados. | OK |
| 9 | Saves locais (Perfis → "Use profile-specific Save Games" ligado): executar o jogo, fazer um save novo, sair; executar de novo e carregá-lo. | O save aparece em `profiles/<perfil>/saves` e na aba Saves; o jogo o carrega. O `skyrimcustom.ini` do perfil tem `sLocalSavePath=__MO_Saves\` (não há um `Skyrimcustom.ini` à parte); no prefixo não aparece pasta `__MO_Saves\` (com a barra no nome). | OK |
| 10 | Desligar os INIs por perfil e abrir o INI Editor; salvar uma mudança inofensiva; depois religar os INIs por perfil. | O editor abre os INIs do prefixo pelo nome real (`Skyrim.ini`, `SkyrimPrefs.ini`; os nomes são procurados sem diferenciar maiúsculas) e a mudança é gravada neles, sem criar `skyrim.ini` minúsculo ao lado. | OK |

**Ao terminar:** registre aqui o resultado de cada item e, em "Observações", o que precisou de correção.

## Observações
- Todos os plugins C++ adicionados na Fase 4c.1 foram validados com sucesso no Bazzite.
- Instaladores BAIN e Bundle operacionais.
- Pré-visualização de imagem, texto e árvore de diretórios do BSA funcionando perfeitamente.
- Extração de BSA via `bsa_extractor` validada.
- `diagnose_basic` alertou sobre o `overwrite` e não incluiu opções incompatíveis de NTFS no Linux.
- INI Editor testado internamente e externamente via `xdg-open`.
- INI Bakery e Saves locais isolando corretamente o prefixo Proton (`__MO_Saves` limpo e sem poluição de nomes case-sensitive).
