# Modlists: compartilhar e importar uma montagem de mods

**Português** · [English](../en/modlists.md) · Anterior: [Jogos](games.md) · Próximo: [Problemas conhecidos](troubleshooting.md)

Uma modlist é um arquivo `.mo2list` que descreve um perfil: quais mods, em que ordem, a
ordem dos plugins, os INIs do jogo, as escolhas de FOMOD e os executáveis. Outra pessoa
importa o arquivo e o Mod Organizer baixa e instala os mods. Situação: implementado, teste
manual pendente. As Collections da Nexus, o Wabbajack e um assistente vêm depois.

O arquivo guarda uma **descrição**, não os mods: os mods da Nexus entram por id, e cada
pessoa baixa da Nexus. Só os mods que você marcar como criados por você levam os arquivos.

## Exportar

**File → Export modlist…** (ao lado de Install Mod) exporta o perfil atual.

1. Preencha nome, autor, versão e, se quiser, um README para quem importar.
2. Por padrão só os mods **ativos** entram. **Include inactive mods as optional** leva
   também os outros; eles chegam desmarcados na revisão de quem importa.
3. Cada mod que não vem da Nexus tem uma escolha:
   - **Manual source** (padrão): a lista diz onde obter o mod (você informa o link) e leva
     a soma de verificação quando o arquivo baixado ainda existe.
   - **Made by me — include files**: os arquivos vão no pacote. Use só para conteúdo que
     você fez ou pode redistribuir. Fica desabilitada nos mods da Nexus, que nunca são
     empacotados.
   - **Leave out**.
4. A pasta `overwrite` aparece arquivo por arquivo, **tudo desmarcado**. O que você marcar
   vai no pacote.
5. A janela mostra os avisos (por exemplo mod com vários arquivos, mod sem soma de
   verificação) e os executáveis com a **linha de comando completa e o ambiente**.
6. Antes de salvar, o export procura o caminho do seu home e o seu nome de usuário nos
   INIs, nos executáveis, nos arquivos de texto incluídos e no manifesto. Se achar, mostra
   onde e pede confirmação (**Save anyway**). Os caminhos absolutos da instância, do jogo,
   do prefixo do Proton e do home viram marcadores (`%INSTANCE%`, `%GAME%`,
   `%PROTON_PREFIX%`, `%HOME%`) e são restaurados na importação.
7. Ao salvar, o MD5 dos arquivos baixados é calculado, com uma janela de progresso que dá
   para cancelar.

Nunca entram: saves, `ModOrganizer.ini`, tokens, chaves de API e logs. As variáveis de
ambiente de um executável cujo nome contém `KEY`, `TOKEN`, `SECRET`, `PASS`, `AUTH`,
`COOKIE` ou `SESSION` são descartadas, com um aviso.

### Limites

- **Mantenha suas edições num mod separado ou no `overwrite`.** Uma edição feita *dentro*
  da pasta de um mod da Nexus (por exemplo um `.ini` do mod editado à mão) **não** vai: quem
  importa recebe o arquivo original. O export não detecta isso.
- As escolhas de FOMOD só são gravadas para mods instalados com esta versão ou mais nova.
  Para os mais antigos o export avisa, e quem importa escolhe as opções à mão.
- Calcular a soma de verificação de arquivos grandes demora.
- Uma modlist ainda não pode ter 4 GiB ou mais: nenhum arquivo incluído pode chegar a
  4 GiB, e os arquivos incluídos (mods empacotados e arquivos marcados do `overwrite`)
  precisam somar menos de uns 4 GiB. Caso contrário, o export recusa com uma mensagem antes
  de calcular as somas de verificação; dê uma fonte manual aos mods grandes ou inclua menos
  arquivos.

## Importar

Dê dois cliques num `.mo2list` no gerenciador de arquivos (abre no Mod Organizer, aberto ou
não) ou use **File → Import modlist…**. O Mod Organizer nunca executa nada da lista.
O clique duplo funciona depois que o Mod Organizer foi aberto ao menos uma vez, porque ele
registra o tipo de arquivo ao iniciar. Se outro aplicativo já é o padrão para arquivos
`.mo2list`, o Mod Organizer não o substitui; use **File → Import modlist…** ou escolha o
Mod Organizer em "Abrir com" no gerenciador de arquivos.

1. **Destino:** uma instância nova, um perfil novo ou um perfil existente. Nada vem
   pré-marcado. Uma lista de outro jogo só entra numa instância nova desse jogo. Uma lista
   feita por um Mod Organizer mais novo é recusada, com a mensagem para atualizar.
2. **Revisão:** os mods, com os opcionais **desmarcados**. Se você já tem um mod em outra
   versão, escolha **Download the list's version** ou **Use mine**. O rodapé diz se você tem Nexus
   Premium ou "No Premium: you will click N downloads on Nexus" (cliques guiados).
   Se a lista traz executáveis, arquivos nativos (`.dll`, `.exe`, scripts...) ou INIs que
   carregam DLLs, uma seção **Executable content** os lista (os executáveis com binário,
   argumentos, pasta de trabalho e ambiente) e você precisa confirmá-la para continuar.
3. **Downloads:** com Premium, automáticos. Sem Premium, uma janela abre a página de cada
   arquivo da Nexus; clique em **Mod Manager Download** nela e o arquivo volta para o Mod
   Organizer. A janela tem **Skip**, **Open again** e **Stop waiting / Cancel**. Os mods de fonte
   manual têm **Open page** e **Choose downloaded file…**. Um arquivo que você já tem (mesma
   soma de verificação) é reaproveitado. A conferência das somas de verificação de arquivos
   grandes roda em segundo plano, com uma janela de progresso.
4. **Instalação:** todos os mods instalam em lote, **sem diálogos**. O que exigiria uma
   decisão vai para o resumo. Ao importar num perfil existente, se alguns mods reaproveitados
   estiverem fora do lugar, uma pergunta aparece antes: "Move N reused mods to the list's
   order?" (Yes move, No mantém a sua ordem).
5. **Resumo:** Installed, Reused, Failed e Skipped, com **Install manually** (abre o
   instalador de sempre para aquele mod), **Retry failed**, **Sort with LOOT** e **Copy**
   (o resumo como texto).

Uma **instância nova** reinicia o Mod Organizer e continua sozinha. Se você fechar o Mod
Organizer no meio de uma importação, a próxima abertura oferece **Continue**, **Later** ou
**Discard**; Continue não repete os mods que já entraram.

Só uma importação roda por vez. Abrir outro `.mo2list` enquanto uma importação está sendo
preparada, está rodando ou mostra o resumo dá "An import is already running. Finish it
(close its windows or its summary) first." A janela principal continua usável durante os
downloads; se você trocar de perfil nesse meio-tempo, o Mod Organizer volta para o perfil
de destino antes de instalar e de aplicar a ordem, os plugins, os INIs e os executáveis.
Se esse perfil foi apagado, a importação para, os mods não instalados aparecem como falhas
e a importação continua registrada (**Continue** na próxima abertura).

### O que a importação nunca faz com a sua montagem

- Nada seu é apagado nem reinstalado por cima. Um mod em outra versão instala **ao lado do
  seu** como `<nome> [<lista>]`, e o seu antigo é desativado **só no perfil de destino**.
- Executáveis importados nunca substituem os seus: se o título já existe (maiúsculas e
  minúsculas não contam), o novo entra como `<título> [<lista>]`. Um executável que roda
  exatamente como um que você já tem (mesmo programa, argumentos e pasta de trabalho, por
  exemplo o jogo ou o SKSE que o próprio Mod Organizer adiciona) não é adicionado de novo; o
  resumo o lista em "Executables already present". O binário precisa estar na
  pasta do jogo, da instância, de `mods` ou de `overwrite`. Das variáveis de ambiente, só uma
  lista permitida de ajustes do Proton, DXVK e VKD3D sobrevive (por exemplo `PROTON_LOG`,
  `DXVK_*`, `VKD3D_*`, `WINEDLLOVERRIDES`, `MANGOHUD`); as outras são listadas no resumo.
- Só os INIs do jogo são aplicados. Num perfil existente, o Mod Organizer pergunta antes.
  Se o perfil não usa INIs locais, os INIs da lista **não** são aplicados (ligar os INIs
  locais é decisão sua) e aparecem no resumo em "INI files not applied".
- Instância nova e perfil novo saem com INIs locais e saves locais.

### Limites conhecidos

- Uma única entrada enorme e maliciosa dentro de um `.mo2list` ainda pode esgotar a
  memória na importação (só derruba o Mod Organizer; abra listas de quem você confia).
- Arquivos da Nexus que foram substituídos ou removidos depois que a lista foi feita vão
  para o resumo como falhas.

Próximo: [Problemas conhecidos](troubleshooting.md).
