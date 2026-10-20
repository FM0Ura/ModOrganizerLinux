# Fase 4c.1 — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-20-phase4c1-cpp-plugins-design.md`](../superpowers/specs/2026-10-20-phase4c1-cpp-plugins-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 compilado na distrobox `mo2-run` (`./build.sh`, resultado em `install/`) e iniciado **pelo box**: `distrobox enter mo2-run -- <repo>/install/bin/ModOrganizer`. Instância de Skyrim SE das fases anteriores.

**Antes de começar:** os itens 1 e 2 pedem um arquivo BAIN e um "bundle" (arquivo com outro arquivo dentro); o item 4 pede um mod com `.bsa`; o item 7 só vale para quem usa o FNIS.

| # | Passo | Esperado | Resultado |
|---|---|---|---|
| 1 | Instalar um mod BAIN (pacote com `00 Core`/`10 …`). | O diálogo BAIN aparece; as opções escolhidas viram os arquivos do mod. | |
| 2 | Instalar um "bundle" (arquivo com outro arquivo dentro). | O MO2 oferece o arquivo interno. | |
| 3 | Pela aba Data, pré-visualizar um `.txt` ou `.ini` e uma imagem; depois ver o conteúdo de um `.bsa` pela pré-visualização. | O texto, a imagem e a lista de arquivos do BSA aparecem na janela de pré-visualização. | |
| 4 | Ligar a extração nas configurações do `bsa_extractor` e instalar um mod com `.bsa`. | Os arquivos extraídos aparecem no mod. | |
| 5 | Deixar arquivos no `overwrite`. Abrir as configurações do plugin `diagnose_basic` (Configurações → Plugins). | Aviso no ícone de problemas; não há opção de atributos de arquivo (`check_fileattributes`) nas configurações do plugin. | |
| 6 | Ferramentas → INI Editor: editar e salvar um INI do perfil; depois, com o modo externo ligado, abrir o INI. | O INI é salvo; no modo externo abre no editor do host (`xdg-open`). | |
| 7 | Só para quem usa FNIS: mudar os dados do FNIS e executar o jogo. | Pergunta "Run FNIS before …?" e o FNIS roda pelo Proton. | |

**Ao terminar:** registre aqui o resultado de cada item e, em "Observações", o que precisou de correção.

## Observações
