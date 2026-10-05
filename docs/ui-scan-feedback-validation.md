# Busca de plugins e resposta dos medidores — 12/09/2026

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

O editor de caminhos agora coloca a inclusão de uma pasta em um card próprio, com título, campo e ações. Iniciar a busca fecha esse editor e abre um modal de progresso de até 480 DIP. Ao terminar, o mesmo modal apresenta os contadores e as ações de tentar novamente, consultar falhas e fechar. O resultado anterior pode ser reaberto sem ocupar a parte inferior do editor de caminhos. Cancelamento tem uma mensagem específica e conserva os resultados recebidos.

A lista de falhas separa caminho, erro e formato/tentativa em linhas distintas. Caminhos longos e Unicode quebram linha. Não há Previous/Next nem campo de detalhes vazio. A lista virtualizada busca páginas de até 100 registros durante a rolagem, preservando os IDs selecionados e conferindo a revisão do scan.

Os medidores mantêm os 28 segmentos e a escala linear da versão 1.2.2. A UI solicita os picos a cada 50 ms por um transporte independente de comandos e diagnósticos. Há apenas uma solicitação em andamento, com prazo de 150 ms; respostas antigas não sobrescrevem dados novos. Fora do Dashboard ou com a janela minimizada, não são feitas novas solicitações dos medidores. O host lê somente os picos atômicos nesse endpoint, sem consultar o driver ou a thread do controlador.

Validação concluída:

- Build Release x64 do host e da UI, mantendo 1.2.2.
- Os 18 testes CTest passaram em 10,49 segundos, incluindo processamento, transporte assíncrono, persistência, descoberta e análise de desempenho.
- Fluxo de abrir caminhos, adicionar Unicode, detectar duplicata, iniciar/cancelar, fechar/reabrir progresso, consultar falhas e recuperar resultado anterior.
- Lista com 205 falhas: carregamento das páginas seguintes pela rolagem e tentativa das seleções 0 e 204, mantendo os IDs.
- Inspeção visual de caminhos, progresso, resultado, cancelamento e falhas em inglês/escuro e português/claro. Settings abriu antes de Plugins e as ações de manutenção funcionaram no perfil simulado.
- Vinte mudanças de nível no teste controlado da UI, com atraso artificial de 900 ms nos diagnósticos: p95 de 91,24 ms e máximo de 112,85 ms. Nenhuma solicitação de medidor após sair do Dashboard. Esses valores medem a resposta visual ao sinal simulado, não a latência de áudio de hardware.
- No host nativo, vinte leituras no pipe de medidores enquanto o pipe de comandos estava ocupado: p95 de 6,29 ms. Comandos de alteração foram rejeitados nesse endpoint. O encerramento normal com conexão pendente terminou em 77,05 ms. O teste usou áudio suspenso.

Os ajustes necessários no executor de UI foram separados de defeitos do aplicativo: o retângulo de ContentDialog inclui o overlay inteiro, o scan simulado precisa liberar seu estado forçado antes de iniciar outra busca e um Border decorativo não aparece necessariamente na árvore de acessibilidade. As verificações finais usam dimensões do conteúdo, reinicializam o cenário simulado e conferem os arquivos compilados junto das capturas.

O ZIP e sua pasta extraída em `out/release-ui-layout-fixes` foram atualizados juntos às 18h32. Os 249 arquivos da extração conferem com o ZIP; executável da UI, XBF e recursos conferem com o build canônico. O host extraído também tem o mesmo SHA-256 do build atual. O MSI foi gerado e seus componentes foram conferidos, sem executar uma instalação.

Foi iniciado o host da própria pasta portátil em perfil temporário, com áudio suspenso. Abrir seu ícone no tray carregou a UI da mesma pasta e com o hash esperado. Settings e o novo editor de caminhos foram abertos e inspecionados; os processos de teste saíram normalmente e as preferências principais permaneceram intactas.

A sessão principal foi salva antes da substituição. O fechamento da UI encerrou o host conforme sua preferência; o executor foi ajustado para aceitar esse encerramento normal. A sessão foi reaberta e, após atualizar o pacote, o novo host retomou as mesmas duas instâncias em ordem, com mute e bypass preservados.

Hashes do pacote final:

- ZIP: `1DCD8429D877A6CDD0EEAE0D558DFEF1ACC3A9CCA1506F694591A78E06A8A13A`
- Host: `4635D7DA2C2FD69281E69C3862D859173CDA9C94122EB1651C429D6F48544D86`
- UI: `F3CB79F252691985723D2E4D08E2BEAF0980E36FA89B28A131A0221F0C042E20`

Evidências:

- Cenários de UI e lista com 205 falhas (local evidence: `out/ui-scan-feedback-20260912/screenshots/results.json`)
- Fluxo final e Settings (local evidence: `out/ui-scan-feedback-20260912/screenshots-final/results.json`)
- Verificação final em português/claro (local evidence: `out/ui-scan-feedback-20260912/screenshots-pt-final/results.json`)
- Resposta visual dos medidores (local evidence: `out/ui-scan-feedback-20260912/screenshots/meter-response.json`)
- Transporte do host nativo (local evidence: `out/ui-scan-feedback-20260912/native/meter-transport.json`)
- Caminhos no portátil entregue (local evidence: `out/ui-scan-feedback-20260912/portable/screenshots/portable-scan-paths.png`)
- Progresso pequeno (local evidence: `out/ui-scan-feedback-20260912/screenshots-final/scan-progress-small.png`)
- Resultado e ações (local evidence: `out/ui-scan-feedback-20260912/screenshots-final/scan-results-small.png`)
- Falhas com caminhos e erros separados (local evidence: `out/ui-scan-feedback-20260912/screenshots-final/scan-failures-separated.png`)
- Falhas em português/claro (local evidence: `out/ui-scan-feedback-20260912/screenshots-pt-final/scan-failures-portuguese-light.png`)
- Abertura da UI pelo host portátil (local evidence: `out/ui-scan-feedback-20260912/portable/host-launch-verification.json`)
- Verificação dos arquivos extraídos (local evidence: `out/release-ui-layout-fixes/portable-verification.json`)
- Comparação com build e MSI (local evidence: `out/release-ui-layout-fixes/ui-layout-verification.json`)
