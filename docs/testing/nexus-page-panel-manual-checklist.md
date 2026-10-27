# Painel da página da Nexus — checklist manual

Spec: [`../superpowers/specs/2026-10-27-nexus-page-panel-design.md`](../superpowers/specs/2026-10-27-nexus-page-panel-design.md)

**Ambiente:** AppImage local gerado do branch `worktree-nexus-page-panel` (`build/appimage/build-appimage.sh`), com o MO2 do usuário fechado. Instância com mods de IDs da Nexus conhecidos, login na Nexus feito (Configurações → Nexus) e `<cache>` = pasta de cache da instância.

## 1. Ícone e painel

- [ ] 1. O ícone aparece só em mods com ID da Nexus; não aparece em separadores, Overwrite, mods externos nem backups.
- [ ] 2. Clique no ícone abre o painel na aba Nexus com imagem, cabeçalho e descrição; não muda a seleção nem o checkbox do mod.
- [ ] 3. Filtros aplicados continuam valendo ao trocar para a aba Nexus e voltar para a aba Filters.
- [ ] 4. Clicar no mesmo ícone de novo esconde o painel; o botão de mostrar/esconder painel (« / ») continua funcionando e seu texto acompanha o estado.
- [ ] 5. Link na descrição abre no navegador, não dentro do painel.
- [ ] 6. Sem login: mensagem de login. Sem rede: mantém o conteúdo salvo e mostra erro no rodapé.
- [ ] 7. Atualizar: busca de novo; com descrição alterada na Nexus (ou `lastNexusQuery` editado no `meta.ini` para 2 dias atrás), o conteúdo é atualizado.
- [ ] 8. Reiniciar o MO2 e trocar de instância com o painel aberto: sem crash.

## 2. Casos adicionais

- [ ] 9. Clicar no ícone do mod A e, antes da resposta, no do mod B: o painel fica em B; o `meta.ini` de A recebe os dados novos.
- [ ] 10. Com o painel aberto num mod, removê-lo: o painel volta à mensagem inicial, sem crash.
- [ ] 11. Refresh com a rede desligada: o conteúdo fica e o estado mostra "Couldn't update: …", sem popup.
- [ ] 12. Imagem: a primeira abertura baixa para `<cache>/nexus-images/`; a segunda não baixa de novo (o arquivo não muda de data).
- [ ] 13. Diálogo de informações do mod → aba Nexus mostra a descrição como antes (Tarefa 2).
