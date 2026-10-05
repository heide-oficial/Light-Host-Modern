# Auditoria 2.0.0 — execução local

Execução local em 01–02/10/2026. Implementação entregue na **build 0025**, com as validações locais descritas abaixo. Não houve publicação remota. O aceite integral do plano permanece pendente das dependências e matrizes de validação ao final deste registro; esta entrega não declara certificação completa para release.

## Alterações entregues

- Entradas: limites iterativos para JSON, inclusive no tratamento de erros; XML sem DTD e com profundidade limitada; validação do tamanho declarado, alfabeto e comprimento do estado JUCE antes de decodificar.
- Áudio: limites para canais, bloco, taxa, latência e buffers; reserva agregada de memória incluindo buffers em substituição; contenção de NaN/Infinity; bypass seco direto; processamento suspenso com segurança se a preparação dos buffers falhar.
- Captura: fila de instâncias alteradas; suspensão limitada à chamada do plugin; codificação e gravação depois da retomada; preservação do estado anterior quando captura ou orçamento falha. Capturas explícitas continuam disponíveis.
- Consultas: hash de estado reutilizado enquanto seu conteúdo não mudar. Cenário com estado de 16 MiB caiu de **973,05 ms** para **13,99 ms** por consulta, em média de cinco consultas locais. Não extrapolar esse resultado para latência de áudio.
- Runtime: MIDI reservado somente para nodes que o utilizam; ordem de processamento e entradas compiladas; trabalho e telemetria restritos aos nodes ativos e consumidores visíveis.
- Dispositivos: transação única com identidade composta e token do inventário; rejeição de lista alterada, nomes ambíguos e duplicidade sem distinguir maiúsculas. Modal libera callbacks ao encerrar.
- Perfis: substituição atômica e backup; recuperação explícita conserva o arquivo danificado e o último backup válido. Catálogo somente leitura não bloqueia a edição da sessão.
- Edições: protocolo 5; perfil e geração de origem no comando; ganho do mixer enviado durante o gesto, com commit separado; envio final antes de fechar/trocar perfil. Recuperação local separada com revisão explícita, sem aplicar uma edição automaticamente em outro perfil.
- Canvas: zoom/pan limitados; cards e fios mantidos por ID e preview independente; pausa visual ao minimizar; conexão por teclado e movimento dos cards pelas setas a partir do botão de opções. Ganhos do mixer e posição não recriam o card focado. Eventos de progresso do salvamento ficam separados da revisão do grafo.
- Atualizador portable: launcher fixo, versões completas em pastas separadas, dois registros duráveis de seleção, confirmação antes de carregar plugins e retorno à versão completa anterior se a tentativa ficar sem confirmação. Preparação ocorre antes do encerramento do áudio.
- Atualizador: assinatura RSA/SHA-256 de manifesto com raízes públicas incorporadas; inventário de arquivos/tamanhos/hashes; bloqueio de downgrade; cache com leases e retenção; aviso/manual continuam independentes da instalação interna. MSI permanece sob Windows Installer. A inspeção do pacote real revelou falha em caminhos temporários acima de 260 caracteres; leitura, escrita e substituição dos arquivos/descritores agora usam caminhos absolutos estendidos, sem alterar o registro do Windows.
- Distribuição: Build Dev e Build Release compartilham o formato versionado do portable. Portables antigos precisam de extração manual do primeiro pacote com launcher. Atualização interna exige NTFS; a assinatura de produção ainda depende de configuração externa.
- Dependências: commits completos de JUCE, VST3 e ASIO, além do hash do pacote Xaymar VST2; nenhuma atualização indiscriminada de versões.
- Isolamento: `LightHostModernWorker.exe`, memória compartilhada pré-alocada, MIDI limitado e validado, fallback local, Job Object com encerramento dos descendentes, controle fora da thread principal e deadlines para carregamento/preparo/editor/estado. Opção experimental por instância em List/Chain, persistida no perfil. A medição ASIO a 64 amostras exigiu MMCSS e margem de dois blocos do worker, com piso de tamanho e latência explícita; ver [contrato de isolamento](../../docs/plugin-isolation.md). Não é um sandbox de segurança.
- Preferências: removido fallback implícito para outra cópia de PropertiesFile, que ressuscitava o modo pendente já consumido no reinício de perfis isolados.

## Medições comparáveis

Canvas estático com 128 elementos (126 mixers e entrada/saída), 127 fios, sem dispositivo de áudio aberto: build 0023 e implementação corrigida na mesma máquina. Cada estado teve 3 s de estabilização e 15 s de amostragem, sem compilação concorrente. CPU normalizada por todos os processadores lógicos. Evidências: `out/audit-canvas-before/results.json` e `out/audit-canvas-after/results.json`.

| Estado | CPU da interface antes → depois | Memória privada do host antes → depois |
| --- | --- | --- |
| Visível | 6,20% → 2,85% | 257,85 → 63,69 MiB |
| Minimizado | 5,81% → 0,11% | 257,81 → 63,65 MiB |
| Restaurado | 5,73% → 2,02% | 257,81 → 63,65 MiB |

A memória privada da interface permaneceu aproximadamente estável: 459 MiB antes e 461–462 MiB depois. Essa medição exploratória não mede fluidez durante arrasto, GPU ou latência de áudio. O ensaio separado de 20 aberturas do modal de dispositivos encontrou e corrigiu retenção: entre as aberturas 4 e 20, o crescimento caiu de 79,56 MiB para 0,76 MiB (`out/test-profiles/chain-audit-ui-6a8e722c9e4c4fab8a08966950837f95/device-modal-memory.json`).

O worker inicial com apenas um bloco de margem perdeu cerca de 9% dos prazos no ASIO de 64 amostras; apenas elevar a prioridade não resolveu. A implementação final usa MMCSS e dois blocos do worker, cujo tamanho mínimo se adapta à taxa. Em 48 kHz/64 amostras isso adiciona 256 amostras (5,33 ms), além da latência do plugin. Na repetição final de 60 s por backend houve 2 blocos atrasados no WASAPI (0,032%) e 2 no ASIO (0,0086%), sem falha de processamento. P99/máximo do callback do host: WASAPI 0,0553/0,1018 ms; ASIO 0,0107/0,1955 ms. Saída muda e entradas desligadas; não equivale a uma avaliação auditiva prolongada nem comprova ausência de glitches. Evidência: `out/audit-integration-release-candidate/isolated-audio/results.json`.

## Histórico de evidências

- Baseline preservado: `out/audit-ctest-results.xml`, `out/audit-digest-results.json`, `out/audit-audio/results.txt`.
- Rodadas anteriores de CTest: 23/23 em 27,94 s e 23/23 em 35,31 s. Ambas antecedem parte das alterações mais recentes e não substituem a rodada final.
- Rodada posterior, sequencial à compilação: 23/23 em 32,41 s (`out/audit-remediation-ctest-results.xml`), antes do isolamento.
- Portable 0024 gerado com launcher real. `Tests/ChainAuditUiTests.ps1` passou por recuperação explícita, movimento por teclado e capturas de seis páginas. Artefatos `out/test-profiles/chain-audit-ui-828660d0819345fba1976060f570ceb2`. A regressão do aviso de reinício foi identificada depois e seu teste foi acrescentado.
- Primeira matriz nativa do worker: passou em 38,49 s, incluindo áudio/MIDI/latência e falhas controladas de criação, preparo, restore, processamento, editor e captura. Esse resultado antecede a integração com AudioEngine/IPC/WinUI e não valida a entrega final. As fixtures passam a usar um executável de teste separado do worker distribuído.
- R02 reproduzido na build 0023: edição pendente do perfil A alterou B. Regressão permanente em `Tests/ProfileEditIsolationTests.ps1`; versão corrigida rejeitou a edição sem vazamento. Artefatos em `out/test-profiles/profile-edit-isolation-*`.
- Integrações de perfis, mixer, sessão e snapshots passaram após a proteção de geração. Benchmark permanente: `Tests/ProfileDigestBenchmark.ps1`, resultado `out/profile-digest-benchmark-results.json`.
- Testes específicos do atualizador: assinatura alterada/chave errada, migração obrigatória do portable antigo, preparação isolada, bloqueio de operação concorrente, tentativa interrompida, registros truncados, payload adulterado, downgrade, escrita bloqueada e cancelamento passaram. Fixture inclui `legacy-payload-files.json`.
- Uma rodada geral executada enquanto a compilação ainda estava terminando foi inválida: binário do scanner bloqueado e timeout do updater sob carga. Foi substituída por reexecuções sequenciais depois da compilação; não conta como aprovação.

## Validação final do código

- Compilação Release x64 de host, WinUI, scanner, worker, helper, launcher e testes concluída. Última suíte nativa: **24/24**, em **102,06 s**, já com a correção de caminhos longos (`out/audit-remediation-final-ctest.xml`).
- **17 componentes de integração aprovados após correções e reexecuções direcionadas**. A rodada consolidada `out/audit-integration-release-candidate/results.json` teve 16 aprovações e uma falha na leitura concorrente de metadados do próprio teste de ciclo de vida. Esse teste foi corrigido e os quatro cenários passaram em `out/audit-lifetime-delivery/results.json`. A descoberta posterior do ciclo de referência do diálogo gerou correção e nova regressão de memória; canvas/recuperação/teclado passaram em `out/audit-ui-delivery.json`. Não apresentar a rodada original como 17/17 sem essas reexecuções.
- UI navegada e capturada por scripts. Matriz adicional: en-US/pt-BR, Compact/Expanded, DPI 192; não abrange todos os monitores e tamanhos de texto. Logs detalhados passaram pelo fluxo de ativação, reinício, cancelamento e salvamento nativo de TXT.
- Hardware: cenários disponíveis em cinco backends, além do ensaio isolado WASAPI/ASIO descrito acima. Algumas mudanças de taxa/buffer não são oferecidas pelo driver e não foram contadas como executadas.
- Plugins instalados testados no worker: **RoughRider3, T-De-Esser e Renegate**, todos aprovados em carregamento, processamento curto, editor, captura e restauração. Resultado: `out/isolated-real-plugins/19cc60923c24406b8202f4f166b6211a/results.json`. Xvox não estava disponível nesta rodada. A validação curta não certifica uso prolongado desses plugins.
- A primeira inspeção do ZIP real falhou em caminho temporário longo (`out/audit-package-before-long-path-fix.json` e `.log`). A correção ganhou regressão nativa; o mesmo pacote passou depois por prepare/apply/rollback (`out/audit-real-package-longpath.log`). A recompilação e o empacotamento seguintes incluem a correção nos binários distribuídos.

## Entrega e inspeção dos artefatos

- Portable extraído: [LightHostModern-build-0025](../../dev-test/LightHostModern-build-0025/LightHostModern.exe). Aberto pelo launcher da raiz em perfil isolado; passaram recuperação explícita, movimento por teclado, conexão por teclado e capturas de seis páginas (`out/test-profiles/chain-audit-ui-b1a5e6a3e574424580ded31d0ff6792b/ui-result.json`).
- ZIP e MSI locais em `out/audit-release`, sem promover a release pública. **6/6 cenários** de inspeção aprovados: tamanhos/hashes, inventário completo e ausência de fixtures, identidade/escopo do MSI, helper real e recusa de assinatura ausente, recusa de UI desatualizada, preparação/aplicação/rollback do ZIP real com confiança efêmera exclusiva do teste (`out/audit-release/package-inspection-results.json`).
- Plano de instalação em VM preparado a partir do MSI final: `out/audit-msi-lifecycle-plan/plan.json`. `executed: false`: nenhuma instalação, atualização, reparo ou remoção do MSI foi executada nesta máquina.
- Sintaxe aprovada em **60 scripts PowerShell**; `git diff --check` terminou com código 0. O wrapper inicial de PowerShell tratou avisos de finais de linha do Git como erro; a chamada foi repetida capturando stderr e o código real, sem ignorar falhas de conteúdo.
- Índice final: `out/audit-final-validation.json`. Hashes dos pacotes: `out/audit-release/release-artifacts.json`. Etapas de entrega: `out/audit-delivery-stages.json`.
- A revisão automática rejeitou a remoção de 14 pastas temporárias `update-test-*` na raiz, com a mensagem “bloqueado por política”. Elas foram preservadas. Novas execuções dos testes usam diretórios de trabalho sob `out`.

## Cobertura por achado

| Achado | Correção e regressão mantida |
| --- | --- |
| A01 | Captura por instância/revisão, suspensão apenas na chamada do plugin, retenção do último estado; `PluginInstanceTests`, `IsolatedPluginTests`, `IsolatedHostIntegrationTests`. Plugins diretos ainda podem bloquear durante a própria captura. |
| A02 | Aritmética verificada, limite de latência e reserva agregada; `RealtimeTests`, incluindo substituição e falha de preparação. |
| A03 | Validação do formato JUCE antes da reserva; `PluginInstanceTests`, `SessionPersistenceTests`, `ProtocolTests`. |
| A04 | Inventário único para o portable e `legacy-payload-files.json`; `PackageInspectionTests` usa o ZIP real em prepare/apply/rollback. |
| A05 | Launcher estável, payloads versionados, dois registros duráveis e confirmação; `PortableUpdateScenarios`. Validação de queda de energia em VM pendente. |
| A06 | Identidade e token de inventário, lote validado; `DeviceRecoveryTests`. Hotplug físico pendente. |
| M01 | Limites antes do parsing JSON/XML; `ProtocolTests`, `StateIntegrationTests` e transporte WinUI. |
| M02 | Saída finita e dry direto no bypass; `RealtimeTests`, `IsolatedPluginTests`. |
| M03 | Hash reutilizado e invalidação por conteúdo; `ProfileDigestBenchmark` e regressões de desfazer/perfil alterado. |
| M04 | MIDI reservado somente onde necessário, dentro do orçamento; `RealtimeTests`. |
| M05 | Ordem e adjacências compiladas, canais/telemetria necessários; `RealtimeTests`, `CallbackMeasurementIntegrationTests`. |
| M06 | Ganho aplicado durante o gesto e commit separado; `MixerIntegrationTests`. |
| M07 | Flush antes de fechar/trocar, cópia de recuperação separada e explícita; `ChainAuditUiTests`, `ProfileEditIsolationTests`. |
| M08 | Cards/fios retidos, atualização de valores sem reconstruir controles, pausa minimizada; `CanvasResourceBenchmark` e `ChainAuditUiTests`. |
| M09 | Ciclos de callbacks do modal removidos, incluindo o evento compartilhado de foco que capturava o próprio diálogo. `ChainAuditUiTests -ExerciseDeviceModal`: 20 aberturas; crescimento entre os ciclos 4 e 20 caiu de 79,56 MiB para 0,76 MiB. Limite de regressão: 8 MiB. Não houve medição de heap prolongada. |
| M10 | Catálogo atômico com backup, modo somente leitura e recuperação explícita; `ProfileIntegrationTests`. |
| M11 | Zoom/pan finitos e limitados, limite defensivo do pontilhado; testes de contrato WinUI e canvas. |
| M12 | Anúncio/manual independentes da autorização de instalar; `UpdateTests` e contrato de atualização. |
| M13 | Cache identificado, retenção e leases, remoção de staging órfão próprio; `UpdateTests`. |
| B01 | Estado de driver separado de processamento/sinal; `DeviceRecoveryTests`, `HardwareIntegrationTests`. |
| B02 | Dependências fixadas, manifesto RSA e fluxo Authenticode; `UpdateTests`. Raízes/certificado de produção pendentes. |
| B03 | Portas nomeadas/focáveis, menus de conexão e movimento por teclado; `ChainAuditUiTests`. |
| B04 | Contratos e scripts atualizados, versão IPC 5, inspeção dos pacotes e matriz de UI por scripts. |
| R01 | Isolamento opcional com fixtures reais de crash/hang, estado/MIDI/latência, processo e descendentes; matriz comercial curta e medições locais. Mantido experimental. |
| R02 | Reproduzido em 0023; comando vinculado a perfil/geração/revisão, rejeição de edição antiga; `ProfileEditIsolationTests`. |

## Dependências externas e limites do aceite

- Não há ambiente descartável de VM disponível nesta máquina para instalar, atualizar, reparar e remover MSI ou testar desligamento abrupto. A inspeção do MSI e a simulação de interrupção dos registros do portable não substituem esses cenários.
- Não há chave de assinatura de release/certificado de produção configurados. As raízes de confiança do updater continuam vazias; instalação interna fica bloqueada com segurança, enquanto avisos e download manual funcionam. Chaves efêmeras de teste nunca são distribuídas como confiança de produção.
- A matriz local não certifica todos os plugins comerciais, sessões de horas, todas as taxas/buffers, hotplug físico, múltiplos monitores ou todos os tamanhos de texto. Medições de CPU/RAM não equivalem a uma análise de GPU/tempo de quadros.
- Isolamento contém falhas nativas apenas das instâncias que usam o worker; execução direta preserva esse risco. O worker acrescenta latência conhecida e pode perder blocos sob pressão de agendamento. Captura lenta ainda pode interromper sua contribuição, sem parar as demais rotas.
- Nenhum teste prova ausência total de falhas. Os itens acima permanecem como gates da publicação, conforme as etapas 4, 8, 9 e 10 do plano.
