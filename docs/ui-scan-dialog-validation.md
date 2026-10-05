# Navegação e busca de plugins — 12/09/2026

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

Support me fica abaixo de Settings. A página Database saiu da navegação. Remove missing e Clear database agora pertencem ao grupo Plugin database em Settings, com o mesmo padrão de ícone, título, descrição e ação dos demais cards. A confirmação de limpeza e o retorno do foco foram preservados.

Installed contém Scan for plugins imediatamente à esquerda de Sort. Running e Installed usam a mesma regra de largura para a busca. O novo modal reúne caminhos e varredura sem abas: campo para adicionar uma pasta, lista de caminhos salvos, ação principal no rodapé e progresso somente quando relevante. Alterações de caminhos são salvas automaticamente. É possível cancelar a varredura, fechar e reabrir o modal enquanto ela continua e consultar falhas sem sobrepor dois ContentDialogs.

O build Release x64 mantém a versão 1.2.2. Cinco cenários de UI passaram; após o ajuste visual da ação principal e da atualização de estado, os três cenários afetados passaram novamente. Foram conferidos navegação, manutenção, cancelamento da confirmação, igualdade das buscas, ordem da toolbar, agrupamento, caminhos Unicode, duplicação de caminho, persistência, início e cancelamento da varredura, reabertura e consulta de falhas. A revisão visual incluiu inglês/escuro e português/claro. O empacotamento executou os 18 testes CTest, todos aprovados em 12,22 segundos.

O ZIP e a pasta portátil em `out/release-ui-layout-fixes` foram gerados juntos. Os 249 arquivos da extração conferem com o pacote entregue; executável, XBF e recursos conferem com o build WinUI atual. O MSI também foi gerado e teve seus componentes conferidos, sem instalação nesta máquina.

A conferência final iniciou o host da própria extração do ZIP em perfil temporário com áudio suspenso. Clicar no ícone desse host no tray abriu a UI do mesmo portátil, com caminho e SHA-256 verificados. Settings e o modal de busca abriram e fecharam normalmente; as capturas foram inspecionadas. O perfil tinha banco vazio, portanto as ações de manutenção ficaram corretamente desabilitadas. O teste de manutenção habilitada usou o host simulado. As preferências originais não foram modificadas.

A sessão principal foi gravada antes de encerrar normalmente o host para substituir os arquivos. Após a atualização, o host foi reaberto com as mesmas duas instâncias, na mesma ordem, e os estados de mute e bypass preservados.

Pacote gerado em 12/09/2026 às 17h42, horário local:

- ZIP: `out/release-ui-layout-fixes/LightHostModern-Portable.zip`
- SHA-256 do ZIP: `1E12E58EC38126033A220B38FEE07FC79F50BD349A4C7DA123DA3A1DE90ADCCE`
- SHA-256 da UI: `32D6012DEC205A68E011B67B04CF1ACC1B71ED692721EF055CE4E404C6B472B8`

Evidências:

- Cenários de UI (local evidence: `out/ui-scan-20260912/screenshots/results.json`)
- Verificação final da UI (local evidence: `out/ui-scan-20260912/screenshots-final/results.json`)
- Settings no portátil (local evidence: `out/ui-scan-20260912/portable/screenshots/portable-settings.png`)
- Modal no portátil (local evidence: `out/ui-scan-20260912/portable/screenshots/portable-combined-scan.png`)
- Modal em português e tema claro (local evidence: `out/ui-scan-20260912/screenshots-final/scan-dialog-portuguese-light.png`)
- UI aberta pelo host portátil (local evidence: `out/ui-scan-20260912/portable/host-launch-verification.json`)
- Verificação da extração (local evidence: `out/release-ui-layout-fixes/portable-verification.json`)
- Comparação com build e MSI (local evidence: `out/release-ui-layout-fixes/ui-layout-verification.json`)

Os pacotes anteriores estão preservados em `out/ui-scan-20260912/packages-before`; a extração anterior está em `out/release-ui-layout-fixes/portable-backups/20260912-174253-8d6cf1b067354d059b995cbb4f309a52`.
