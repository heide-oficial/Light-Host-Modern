# Entrega portátil corrigida — 12/09/2026

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

Registro histórico da correção de empacotamento das 16h48. A entrega seguinte, com a reorganização de navegação e busca, está documentada em [Navegação e busca de plugins](ui-scan-dialog-validation.md), incluindo os hashes atuais.

Os crashes das 16h44 e 16h45 ocorreram na UI antiga de `out/release-ui-layout-fixes/LightHostModern-Portable`. A primeira correção tinha atualizado somente a pasta extraída: o ZIP nessa mesma pasta ainda continha o executável anterior. Extrair esse ZIP restaurava a UI com o acesso inválido à página Support ainda não criada quando `HideSupportTab=1`.

A comparação dos arquivos confirmou o executável antigo com SHA-256 `1961863775EC2371FE29E96F90E622C852139719D4DE67E66DB553772C368B2A` e o corrigido com SHA-256 `EFA10871D20A49B9843839BA4FA122C2CB96E9FC36B414A440237B8DA950C960`. Não foi necessária outra alteração na UI para esta reincidência.

`Utilities/Build Release.ps1` agora extrai o ZIP entregue, compara a quantidade e os hashes de todos os arquivos com o payload e publica essa extração em `LightHostModern-Portable`. A pasta anterior é preservada em `portable-backups`. A publicação recusa a substituição se houver processos executando dessa pasta. `release-info.json` identifica o hash da UI; `portable-verification.json` registra o hash do ZIP e de todos os arquivos conferidos.

O empacotamento foi executado novamente em Release para `out/release-ui-layout-fixes`, mantendo a versão 1.2.2. A validação de origem do build passou, assim como os 18 testes CTest (9,81 segundos). Os 249 arquivos da extração conferem com o ZIP entregue. Executável, layouts XBF e recursos também conferem com a saída atual do WinUI. O MSI foi gerado e seus componentes foram conferidos, sem instalação nesta máquina.

A validação de execução iniciou `Light Host Modern.exe` da extração desse ZIP, em perfil temporário, com cópia das preferências de UI do usuário e áudio suspenso. A interface foi aberta pelo ícone do host no tray. O caminho e o hash do processo confirmaram que o host abriu sua própria UI portátil atualizada. Settings passou no primeiro acesso e nos retornos a partir de Database, Plugins, Audio e Dashboard, com Support oculto. As capturas foram inspecionadas visualmente. UI e host de teste encerraram normalmente e as preferências originais mantiveram seu hash. Este teste não abriu dispositivos de áudio.

O ZIP entregue é `out/release-ui-layout-fixes/LightHostModern-Portable.zip`, SHA-256 `004A5DB5FF42A2885EC99E9B2C1B249B3ED8E6A59EC3EB9CC75F20E58335F2DB`, gerado em 12/09/2026 às 16h48, horário local.

- Comparação dos ZIPs anteriores (local evidence: `out/portable-fix-20260912/archive-before.json`)
- Verificação de todos os arquivos da extração (local evidence: `out/release-ui-layout-fixes/portable-verification.json`)
- Comparação com o build e componentes do MSI (local evidence: `out/release-ui-layout-fixes/ui-layout-verification.json`)
- Processo aberto pelo host portátil e resultado do teste (local evidence: `out/portable-fix-20260912/host-launch-verification.json`)
- Acessos a Settings (local evidence: `out/portable-fix-20260912/screenshots/results.json`)
- Captura de Settings com Support oculto (local evidence: `out/portable-fix-20260912/screenshots/portable-settings-hidden-support.png`)
- Captura após a navegação (local evidence: `out/portable-fix-20260912/screenshots/portable-settings-after-navigation.png`)

Os pacotes anteriores estão preservados em `out/portable-fix-20260912/archives-before`; a pasta anterior está em `out/release-ui-layout-fixes/portable-backups/20260912-164923-d21eaf227db04270b66348e4e6cb6e47`.
