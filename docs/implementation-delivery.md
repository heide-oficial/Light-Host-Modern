# Entrega local — Light Host Modern 1.2.2

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

Entrega consolidada em 8 de setembro de 2026. A pedido do usuário, a rodada de testes foi encerrada. A versão permanece **1.2.2**, com C++/JUCE no áudio e C++/WinRT com WinUI 3 na interface. O código e os pacotes locais estão disponíveis; a matriz completa de aceitação do plano não foi aprovada.

## Implementação entregue

- Perfis temporários isolados, IPC 4 com operações consultáveis e recuperação de dispositivos por geração de configuração.
- Instâncias por UUID, ações estáveis em Installed, processamento e ciclo de vida coordenados, buffers e MIDI limitados, transições de latência e auditoria de alocações do host.
- Descoberta em processo isolado, cache com metadados de buses, falhas paginadas e novas tentativas por ID.
- Eventos independentes dos comandos, páginas criadas sob demanda, listas virtualizadas, renomeação, troca de posição, agrupamento, detalhes, medidores e diagnósticos separados.
- Sessão versionada com migração recuperável, preservação de estados, gravação atômica, backup e tratamento de falhas.
- Atualizador com streaming, cancelamento, SHA-256, seleção MSI/ZIP, auxiliar de instalação e empacotamento com verificação do executável WinUI atual.

Preferências, estados e dados legados foram preservados pelos mecanismos de migração e isolamento. CLAP, efeitos internos, saída secundária de monitoramento e biblioteca de presets continuam fora deste trabalho.

## Pacotes finais

| Artefato | Arquivo |
|---|---|
| Portátil x64 | LightHostModern-Portable.zip (local evidence: `out/release-test-final-audit/LightHostModern-Portable.zip`) |
| Instalador x64 | LightHostModern-Setup.msi (local evidence: `out/release-test-final-audit/LightHostModern-Setup.msi`) |
| Tamanhos e SHA-256 | release-artifacts.json (local evidence: `out/release-test-final-audit/release-artifacts.json`) |

Os pacotes Release incluem host, UI, scanner e auxiliar, sem fixtures de terceiros. São pacotes locais de teste, sem assinatura, com auditoria de alocações do host habilitada. Nada foi instalado ou publicado. Não foi criado commit.

## Verificação realizada

- **18 suítes CTest passaram** na execução final do empacotamento, em 12,99 segundos. As quatro verificações de integridade e conteúdo dos pacotes também passaram.
- Na UI final, passaram **9 cenários de páginas**, **3 de temas/diagnósticos**, **4 do atualizador** e **8 de idioma/layout/teclado a 200%**. O lançamento real pelo ícone da bandeja abriu a UI correta com o perfil isolado.
- Passaram cenários de listas com **100, 500 e 1.000 processadores simulados**, testes de persistência e recuperação, descoberta/cache dos oito módulos Dragonfly e testes com o Airwindows PurestGain oficial.
- Passaram oito cenários de processamento/estado/editor com plugins reais, além do Early Reflections VST2 usando o editor genérico. A auditoria desses cenários registrou zero alocações/liberações do host; ela não cobre o interior das DLLs dos plugins.
- Backends disponíveis foram exercitados em perfis separados. Shared WASAPI e DirectSound registraram xruns, preservados nos relatórios. Os testes não alteraram as preferências da sessão principal.

Os resultados históricos e caminhos das evidências estão em [completion-progress.md](completion-progress.md). Os processos temporários do aplicativo estavam encerrados na conferência final.

## Limites da entrega

- **Desempenho:** apenas a série completa do Dashboard do baseline foi concluída. Faltam as séries completas equivalentes do build atual e com UI minimizada; não há aprovação do limite de regressão de 5% nem de estabilidade prolongada de memória/filas. Consulte [performance-validation.md](performance-validation.md).
- **Instalação:** instalação, atualização, reparo e remoção do MSI não foram executados em Windows descartável. A inspeção do pacote não substitui esses cenários.
- **Hardware e acessibilidade:** reconexão física, leitor de tela, matriz completa de alto contraste e múltiplos monitores não foram validados.
- **Bandeja e editor:** as ações do menu nativo da bandeja ficaram sem confirmação pela automação. O editor nativo do Dragonfly Early Reflections VST2 travou no driver gráfico VMware; o editor genérico passou. Consulte [real-plugin-validation.md](real-plugin-validation.md).
- **Organização da UI:** serviços, páginas e controladores foram extraídos, mas MainWindow ainda contém apresentação e coordenação que admitem extração adicional; a separação integral prevista no plano não está concluída.

Esses pontos permanecem registrados como incompletos, sem iniciar novas rodadas de teste nesta entrega.
