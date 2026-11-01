# Modlists, parte 1 — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-28-modlists-design.md`](../superpowers/specs/2026-10-28-modlists-design.md) (§9) · Plano: [`../superpowers/plans/2026-10-31-modlists-parte1-nucleo-mo2list.md`](../superpowers/plans/2026-10-31-modlists-parte1-nucleo-mo2list.md) · Guia do usuário: [`../user/pt-BR/modlists.md`](../user/pt-BR/modlists.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`, com o `install/` do worktree `modlists-parte1` (`./build.sh` na distrobox `mo2-run`). Skyrim Special Edition (Steam) rodado ao menos uma vez pela Steam. Uma instância de Skyrim SE com um perfil real (mods da Nexus, ao menos um mod instalado por FOMOD depois desta versão, um mod local sem origem na Nexus, plugins ordenados, INIs editados, um executável do SKSE). Duas contas da Nexus: uma Premium e uma **sem Premium** (a sem Premium é a do teste principal). Downloads pelo botão **Mod Manager Download** do site da Nexus (handler `nxm://` da Fase 4a).

## 0. Fixture e página de arquivo da Nexus

- [ ] Confirmar o formato de `nexusFilePageUrl`: abrir `https://www.nexusmods.com/skyrimspecialedition/mods/12604?tab=files&file_id=<id do arquivo>` no navegador. Esperado: abre a aba de arquivos do mod com aquele arquivo em foco e o botão **Mod Manager Download** disponível (é o endereço que o assistente de downloads guiados abre para cada arquivo).
- [ ] Conferir na Nexus os ids de [`deps/modorganizer/tests/fixtures/modlists/README.md`](../../deps/modorganizer/tests/fixtures/modlists/README.md): SkyUI (mod 12604), Better MessageBox Controls (mod 1428), Better Dialogue Controls (mod 1429) e os ids de arquivo da tabela (35407, 1429, 1430, que o README já diz serem prováveis erros). Se algum estiver errado, corrigir a tabela do README **e** o teste `Mo2ListZip.DISABLED_WriteFixture`, e regerar o `small.mo2list` (o README diz como).

## 1. Exportar um perfil real de Skyrim

- [ ] **File → Export modlist…** no perfil real. Preencher nome, autor, versão e README. Esperado: a janela lista os mods; os mods da Nexus aparecem sem escolha de origem; cada mod que não é da Nexus tem **Manual source / Made by me — include files / Leave out** (o padrão é **Manual source**; **include files** fica desabilitado nos mods da Nexus); a pasta `overwrite` aparece arquivo por arquivo, **tudo desmarcado**; os avisos aparecem; os executáveis aparecem com a linha de comando completa e o ambiente.
- [ ] Marcar **Include inactive mods as optional** e conferir que o resumo passa a contar os inativos.
- [ ] Salvar. Esperado: a passada do MD5 mostra progresso; o arquivo `.mo2list` é criado.
- [ ] Sem nenhum mod local marcado como **Made by me**, o pacote não contém `bundled/`: `unzip -l <arquivo>.mo2list`.

## 2. Sem vazamento de home nem de nome de usuário

- [ ] `unzip -p <arquivo>.mo2list manifest.json | grep -i <usuário>` e `unzip -p <arquivo>.mo2list 'profile/*' | grep -i <usuário>` não imprimem nada; o mesmo para `grep -i /home`. Caminhos aparecem como `%INSTANCE%`, `%GAME%`, `%HOME%` ou `%PROTON_PREFIX%`. O pacote não tem saves, `ModOrganizer.ini`, token nem log.
- [ ] Variáveis de ambiente de executável com `KEY`, `TOKEN`, `SECRET`, `PASS`, `AUTH`, `COOKIE` ou `SESSION` no nome (criar uma de teste num executável) não estão no `manifest.json`, e o export avisou.
- [ ] Colocar um caminho do home dentro de um INI do `overwrite` (por exemplo `sTest=/home/<usuário>/x`), marcar o arquivo e salvar. Esperado: aparece a confirmação do vazamento, dizendo onde está (arquivo e trecho), com **Save anyway**; cancelar não grava o arquivo.
- [ ] Exportar de novo e cancelar durante a passada do MD5 (um mod com arquivo grande ajuda). Esperado: o botão mostra "Cancelling…" até parar; nenhum `.mo2list` fica pela metade.

## 3. Importar numa instância nova, conta sem Premium

- [ ] **File → Import modlist…**, escolher o arquivo do item 1. Esperado: o destino **não vem pré-marcado** (nova instância / novo perfil / perfil existente); escolher **nova instância**.
- [ ] Janela de revisão: linhas opcionais desmarcadas; rodapé "No Premium: you will click N downloads on Nexus"; se houver executáveis, a seção **Executable content** exige confirmação para continuar.
- [ ] Downloads: abre a janela guiada, que abre a página de cada arquivo da Nexus (**Mod Manager Download**); **Skip**, **Open again** e **Stop waiting / Cancel** funcionam; um `nxm://` repetido não conta duas vezes e um item pulado não ressuscita sozinho.
- [ ] Com a janela guiada aberta e a pergunta "Files left" visível, clicar num **Mod Manager Download** de um arquivo da lista. Esperado: o `nxm://` é tratado sem queda do MO2.
- [ ] Mods de fonte manual: **Open page** e **Choose downloaded file…** funcionam.
- [ ] A instalação em lote roda **sem nenhuma pergunta modal** (só a janela de progresso). O que precisaria de decisão vai para o resumo.
- [ ] O MO2 reinicia sozinho na instância nova e a importação continua. Resumo: Installed / Reused / Failed / Skipped, **Install manually**, **Retry failed**, **Sort with LOOT** e **Copy** funcionam.
- [ ] Repetir com a conta **Premium**: os downloads são automáticos, sem a janela guiada.

## 4. Comparar com o original

- [ ] Prioridade dos mods, separadores e ativo/inativo iguais ao perfil original.
- [ ] Ordem dos plugins e plugins ativos iguais (rodar **Sort with LOOT** não é necessário para igualar).
- [ ] INIs do jogo (`Skyrim.ini`, `SkyrimPrefs.ini`, `SkyrimCustom.ini`) iguais, com os caminhos voltando para a instância nova; só os INIs do jogo foram aplicados.
- [ ] Escolhas de FOMOD: os mods instalados por FOMOD depois desta versão voltaram com as mesmas opções, sem janela de instalação. Mods de FOMOD antigos (sem escolhas gravadas) aparecem no resumo para **Install manually**.
- [ ] Os executáveis importados estão na lista, com a linha de comando e o ambiente mostrados na revisão. Do ambiente, só as variáveis de ajuste do Proton/DXVK/VKD3D da lista permitida sobreviveram (as outras estão no resumo).

## 5. Jogar

- [ ] Executar o Skyrim pelo MO2 na instância importada e chegar ao menu principal e a um jogo carregado, com os mods ativos funcionando (por exemplo SkyUI).

## 6. Importar num perfil existente

- [ ] Preparar um perfil com alguns mods que a lista também tem (mesma versão) e um com versão **diferente** da lista. **Import modlist…** → **perfil existente**. Esperado: o MO2 pergunta antes de aplicar os INIs; os mods iguais aparecem como **Reused** e não são baixados; os de versão diferente mostram a escolha **Download the list's version** / **Use mine**.
- [ ] Escolher **Download the list's version** num deles. Esperado: instala ao lado como `<nome> [<lista>]`; o mod antigo fica **desativado só neste perfil** (continua ativo nos outros perfis); nada seu foi apagado nem reinstalado por cima. Com **Use mine**, o mod continua como estava e nada novo entra.
- [ ] Se houver mods reaproveitados fora da ordem da lista, aparece uma pergunta "Move N reused mods to the list's order?"; **Yes** os move, **No** mantém a ordem. Se o perfil não usa INIs locais, os INIs da lista não são aplicados e aparecem no resumo em "INI files not applied".
- [ ] A instalação em lote não mostrou nenhuma pergunta modal (só a janela de progresso).

## 7. Conteúdo executável em `bundled`

- [ ] Marcar um mod local como **Made by me — include files** com um `.dll` dentro (por exemplo um plugin do SKSE), exportar e importar. Esperado: a seção **Executable content** da revisão lista a DLL e só deixa continuar depois da confirmação explícita. Nada é executado pela importação.

## 8. `schemaVersion` maior

- [ ] Editar o `manifest.json` de um `.mo2list` (`unzip`, mudar `"schemaVersion": 1` para `2`, `zip -r` de novo) e importar. Esperado: recusado com "This modlist was made by a newer Mod Organizer. Update Mod Organizer to import it."

## 9. Fechar no meio da importação

- [ ] Iniciar uma importação numa instância nova e fechar o MO2 no meio da instalação (ou dos downloads). Reabrir. Esperado: o MO2 oferece **Continue / Later / Discard**; **Continue** termina a importação sem duplicar os mods que já entraram; **Later** mantém a oferta para a próxima abertura; **Discard** a descarta.

## 10. Abrir pelo gerenciador de arquivos

- [ ] No Dolphin, clicar duas vezes num `.mo2list` com o MO2 **aberto** (iniciado no box): a janela de importação abre no MO2 que já roda.
- [ ] O mesmo com o MO2 **fechado**: ele abre e, depois da janela principal, abre a importação.
- [ ] O mesmo com um arquivo de nome com espaço, `My List.mo2list`, nos dois casos acima.
- [ ] O registro do tipo de arquivo acontece ao iniciar o MO2 (precisa ter sido aberto ao menos uma vez) e não substitui outro aplicativo que já seja o padrão para `.mo2list`: com outro aplicativo como padrão, o clique duplo continua abrindo esse aplicativo e **File → Import modlist…** funciona.

## 11. Executáveis importados

- [ ] Importar uma lista com um executável de mesmo título de um que você já tem, e outra vez com o título só diferindo em maiúsculas/minúsculas. Esperado: entra um novo `<título> [<lista>]` e o seu fica intacto (binário, argumentos e ambiente). Um executável cujo binário fica fora das pastas do jogo, da instância, de `mods` e de `overwrite` não é adicionado e vai para o resumo.

## 12. Limites conhecidos (só conferir que a documentação bate)

- [ ] Editar à mão um INI dentro da pasta de um mod da Nexus, exportar e importar: o INI editado **não** vai (quem importa recebe o original), como diz o guia do usuário. Repetir com a edição num mod separado ou no `overwrite`: vai.
