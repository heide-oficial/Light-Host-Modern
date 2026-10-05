# Plano — correções do scanner e coleta de logs detalhados

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

Data: 23/09/2026. Base: LightHostModern 1.4.0, commit `2b7eea1`.
Status: implementação local com validação parcial; resultados e limitações registrados em [issue-7-validation.md](issue-7-validation.md). A rodada adicional de desempenho inclui leitura com buffer, validação de VST3 de classe única no processo de catálogo, cache incremental e medições por etapa. Os novos testes de regressão estão preparados, com execução adiada a pedido do usuário. Compatibilidade comercial e a matriz completa de desempenho/validação continuam pendentes.

Ajuste posterior solicitado pelo usuário: gerenciamento e estado dos logs ficam somente no topo de Diagnostics, sem link ou informação em Settings. O card usa ícone, texto e toggle com On/Off; “Waiting for restart” é um link azul que abre o modal, substituindo o botão de reinício no card. Essa decisão substitui a proposta de acesso por Settings abaixo. Uma captura pendente continua mantendo Diagnostics acessível.
Issue: https://github.com/heide-oficial/Light-Host-Modern/issues/7

## Objetivo e escopo

Corrigir os defeitos identificados na busca e validação de plugins, reduzir trabalho repetido, tornar os resultados compreensíveis e permitir que o usuário grave e exporte uma captura detalhada do aplicativo para investigação.

Entregas: código, regressões dos defeitos, validações de compatibilidade/desempenho, interface em inglês/PT-BR, documentação e evidências locais. A versão de distribuição será definida na preparação de uma futura release; não alterar a tag/release 1.4.0. Este plano não inclui publicação, comentários ou fechamento da issue, nem instalação de plugins comerciais na máquina do usuário.

## Base factual

| Problema | Evidência | Prioridade |
|---|---|---|
| Pacote VST3 e seu binário interno tratados como identidades diferentes | Reproduzido com scanner do portable 1.4.0 e manifesto válido, mantendo o mesmo binário e IDs de classe | Crítica |
| Prazo da enumeração não é renovado pelo avanço dentro da pasta | Código e reprodução isolada da supervisão; timeout de pasta aparece no vídeo | Crítica |
| Falha de pasta permanece após a pasta voltar a funcionar | Reproduzido no host publicado, em perfil sem áudio | Alta |
| Retry de pasta força revalidação de módulos saudáveis | Código e reprodução com cache funcional em busca nova | Alta |
| Leituras, enumerações, instanciações e arquivos temporários repetidos | Confirmado no código; custo relativo ainda requer medição | Alta |
| Crash/timeout de uma classe perde resultados do módulo ainda não publicados | Confirmado no desenho; relação com Waves ainda não reproduzida | Alta |
| Pastas opcionais ausentes, arquitetura e falhas reais misturadas | Código e anexos; arquitetura dos Townsend Labs ainda desconhecida | Alta |
| Contadores acumulam tentativas e podem sugerir busca completa quando não foi | Confirmado no código | Média |
| Junctions/symlinks ignorados sem explicação específica | Confirmado no código, sem evidência de uso pelo autor | Média |
| Falta de histórico detalhado e exportável entre os processos | Confirmado na revisão dos logs existentes | Alta |

As evidências experimentais locais estão em `out/issue-7/investigacao.md`, `identity-repro/`, `retry-results.json` e `watchdog-results.txt`. A implementação deve transformar os cenários importantes em regressões versionadas, sem depender desses arquivos locais.

## Decisões que devem ser preservadas

- A busca continua isolada do processo de áudio; não carregar plugins desconhecidos no host para acelerar descoberta.
- Preservar estados de plugins, aliases, IDs de instâncias, cadeia, preferências e resultados válidos de buscas anteriores.
- Não substituir a verificação de identidade por comparação somente de nome/fabricante.
- Não criar retries automáticos ilimitados; repetição manual deve ser direcionada e informativa.
- Não implementar ponte para plugins de 32 bits neste escopo. Identificar a incompatibilidade corretamente.
- Não implementar os logs apenas como ativação do `--debug`: a escrita atual é síncrona e a cobertura do scanner é insuficiente.
- Logs verbose iniciam somente no próximo início completo do app e terminam sem reinício.
- Exportação em um arquivo `.txt` UTF-8, escolhido pelo diálogo nativo Salvar como do Windows.
- Nada será enviado automaticamente; o usuário decide compartilhar o arquivo.

## Etapa 1 — contratos comuns e regressões iniciais

1. Registrar as reproduções de identidade, progresso contínuo e falha de raiz resolvida em fixtures próprias ou referências redistribuíveis.
2. Definir identidades separadas para captura de logs, execução do host, busca, tentativa, raiz, módulo e classe; usá-las de forma consistente no scanner e no histórico.
3. Definir resultado por raiz/módulo/classe e estados explícitos: descoberto, em análise, aceito, cache, ignorado com motivo, falha, cancelado e incompleto.
4. Acrescentar etapas do worker: enumeração, fingerprint, catálogo de classes, carregamento/validação e finalização.
5. Versionar os formatos do protocolo/cache que mudarem. Adaptar schema IPC, validação dos argumentos, eventos e paginação conforme o contrato atual. Incompatibilidade deve produzir mensagem explícita, sem fallback silencioso.
6. Separar as responsabilidades em arquivos pequenos: identidade de plugin, coordenação da busca, protocolo de worker, cache, classificação de resultados e coleta/exportação de logs. Integrar com a organização existente de `Source/`, WinUI e `Tests/`, evitando concentrar a funcionalidade em `MainWindow.xaml.cpp`.

Aceite: fixtures reproduzem os problemas antes das correções; contratos não confundem uma classe com um arquivo ou uma pasta; resultados antigos continuam legíveis ou são revalidados de maneira controlada.

## Etapa 2 — identidade VST3 correta e compatibilidade de dados

1. Criar uma verificação comum de equivalência módulo/classe usada no scanner e na restauração.
2. Aceitar, após verificação do módulo, a pasta VST3 e seu binário correspondente, normalização válida de caminhos Windows e diferenças legítimas de representação.
3. Confirmar arquitetura e localização do binário e comparar a classe correta. Para VST3, manter o CID completo como evidência de validação quando disponível, além dos IDs usados pelo JUCE; para VST2, preservar UID e identidade de shell/subclasse.
4. Tratar divergência de manifesto de forma explícita: confirmar classes pela factory em processo isolado quando aplicável, sem aceitar outra classe por aproximação. Não modificar arquivos de terceiros.
5. Separar a identidade persistida usada por sessões/aliases da comparação técnica de equivalência. Reconhecer representações antigas sem duplicar entradas ou reaplicar estado a outra classe. Se uma migração for indispensável, manter backup e associação inequívoca, com regressões específicas.
6. Atualizar informações verificadas de buses/canais sem fazer os zeros de um manifesto invalidarem um efeito que expõe canais ao carregar.
7. Logar campos esperados/reais e motivo exato da aceitação ou rejeição.

Aceite: mesmo plugin com e sem manifesto é reconhecido; pacote/binário equivalente funciona; classe substituída ou módulo externo é rejeitado; nomes personalizados, estado e cadeia existentes são preservados.

## Etapa 3 — progresso, timeout e cancelamento

1. Renovar a supervisão por trabalho real: avanço em diretórios, candidatos, bytes efetivamente lidos e etapas concluídas. Um sinal periódico de processo vivo sozinho não comprova avanço.
2. Separar prazo de inatividade e limite total por operação/etapa. Manter 60 segundos como referência inicial de inatividade; calibrar limites totais de enumeração e validação pela matriz de bibliotecas e classes, centralizados no código e registrados no relatório de validação antes da entrega.
3. Uma pasta saudável não deve ser considerada travada apenas por exceder 60 segundos no total. Se atingir um limite total de proteção, marcar busca incompleta, explicar o motivo e permitir continuação sem perder o trabalho válido.
4. Aplicar cancelamento também durante enumeração, leitura longa, validação e persistência de resultados, encerrando processos auxiliares e descendentes corretamente.
5. Preservar resultados já confirmados e diferenciar timeout de enumeração, carregamento de classe, crash e cancelamento.

Aceite: uma raiz que progride por mais de 60 segundos termina normalmente; uma operação realmente bloqueada termina dentro do prazo; cancelamento não apaga sucessos anteriores nem deixa workers órfãos.

## Etapa 4 — retry correto, falhas atuais e cache

1. Registrar conclusão por raiz e resolver suas falhas anteriores somente quando a etapa correspondente terminar com sucesso.
2. Atualizar tentativa, histórico e resultado atual separadamente. Uma falha resolvida deixa a lista ativa, mas permanece no log da captura.
3. Retry de enumeração repete a descoberta da raiz com cache de módulos válidos. Retry de módulo/classe força apenas os itens realmente selecionados ou ainda falhos.
4. Deduplicar o trabalho quando forem selecionados a pasta e arquivos contidos nela. Manter acesso a todas as falhas, inclusive além da primeira página.
5. Guardar resultados válidos por classe em módulos parcialmente aceitos; mudanças reais do módulo invalidam os registros afetados.
6. Manter repetição segura após cancelamento, queda do host ou reconexão da interface, usando os IDs/revisões existentes para impedir ações obsoletas.

Aceite: pasta corrigida elimina seu erro; retry não reabre módulos saudáveis sem necessidade; falhas restantes recebem tentativa correta; cache não reutiliza conteúdo alterado.

## Etapa 5 — pacotes com várias classes e resultados parciais

1. Separar descoberta do catálogo e validação de classes no protocolo. Preferir manifesto/factory para obter o catálogo sem instanciar todas as classes apenas para enumerá-las.
2. Usar adaptação localizada para VST3 e VST2 shell quando JUCE não expuser a separação necessária, mantendo essa lógica fora da interface e do processamento de áudio.
3. Validar classes identificadas isoladamente, com prazo individual e checkpoint persistido após cada sucesso; uma classe que trava não pode descartar irmãs já validadas.
4. Publicar incrementos com sequência, IDs e validação de origem; não aceitar XML incompleto nem resultados de uma tentativa antiga.
5. Se a própria factory falhar antes de informar as classes, registrar falha do catálogo e essa limitação; não inventar que as classes desconhecidas foram testadas.
6. Retomar somente classes pendentes/falhas, aproveitando o cache das confirmadas.

Aceite: fixture com várias classes preserva sucessos quando uma classe retorna erro, trava ou derruba o worker. Validar WaveShell real quando houver ambiente legítimo disponível; registrar claramente se essa validação permanecer pendente.

## Etapa 6 — desempenho da busca

1. Medir antes/depois por etapa, com cache frio/quente, bibliotecas pequenas/grandes, pastas sobrepostas e módulos com várias classes. Separar tempo de I/O, hash, inicialização de processo e plugin.
2. Consolidar raízes e formatos antes do trabalho pesado, deduplicando módulos por identidade de arquivo, sem desconsiderar a preferência VST2 do usuário.
3. Reduzir cálculos de fingerprint repetidos e leituras desnecessárias durante a mesma operação. Qualquer reaproveitamento deve preservar detecção de alteração; tamanho/data sozinhos não serão tratados como prova suficiente em situações ambíguas.
4. Evitar instanciação duplicada somente para descoberta; reaproveitar descrições verificadas dentro da tentativa quando correto.
5. Publicar resultados em lotes pequenos com limite de tempo/tamanho, mantendo responsividade, em vez de um arquivo temporário por candidato.
6. Permitir avanço da validação com candidatos já descobertos usando fila limitada, conservando a enumeração observável e cancelável.
7. Medir o custo de gravação repetida do catálogo e agrupar persistência sem perder checkpoints de progresso.
8. Começar com concorrência limitada e previsível; aumentar o número de workers somente se as medições demonstrarem ganho sem prejudicar áudio, memória, licenciamento ou plugins sensíveis.

Aceite: busca quente evita instanciações desnecessárias; contagens de leituras/processos caem onde houve otimização; memória e filas são limitadas; não há regressão material de estabilidade ou de áudio. Publicar medições, sem prometer porcentagens antes de obtê-las.

## Etapa 7 — pastas, arquitetura, mensagens e progresso da interface

1. Preservar a origem das raízes: padrão opcional ou adicionada pelo usuário. Pasta padrão ausente aparece como ignorada; pasta escolhida indisponível recebe aviso acionável. As verificações de filesystem permanecem no worker.
2. Não percorrer automaticamente pastas exclusivas de VST2 quando esse suporte estiver desligado. Mostrar de forma simples quais formatos estão habilitados; pastas personalizadas podem conter ambos.
3. Identificar a arquitetura PE antes de carregar e reportar incompatibilidade de 32/64 bits corretamente. Não inferir arquitetura somente pelo nome da pasta.
4. Tratar junctions/symlinks com identidade canônica, detecção de ciclos e limites de percurso, permitindo destinos válidos escolhidos pelo usuário. Se um link não puder ser percorrido, informar motivo e caminho, em vez de omitir silenciosamente.
5. Distinguir pasta ausente, permissão, rede indisponível, arquitetura, nenhuma classe de plugin, dependência/carregamento, divergência de identidade, timeout por etapa, crash e cancelamento. Mostrar código técnico em detalhes copiáveis, não apenas `crash` para diferentes falhas.
6. Manter separados módulos examinados, plugins reconhecidos, cache, itens ignorados e falhas atuais. Repetições não aumentam artificialmente a contagem de plugins únicos.
7. Durante enumeração, usar progresso indeterminado ou explicitamente parcial. Não exibir sucesso completo quando ainda houver raízes incompletas; não inventar quantidade de arquivos desconhecidos.
8. Oferecer retry conforme o tipo de problema e explicar quando é necessário mudar algo antes de tentar novamente.

Aceite: o cenário visual da issue produz mensagens distintas e contagens compreensíveis; todos os textos, tooltips e nomes acessíveis existem em inglês e PT-BR; layout acompanha os padrões atuais.

## Etapa 8 — infraestrutura de logs detalhados

### Coleta e conteúdo

- Criar uma sessão de captura compartilhada por host, interface e workers, com ID persistente e segmentos por processo/inicialização. Capturar desde o início do host quando a coleta estiver armada, antes da restauração da cadeia e do uso do scanner.
- Registrar horário UTC com precisão de milissegundos, ordem local/tempo monotônico, processo/thread, componente, nível, operação e mensagem com campos estruturados. O `.txt` apresenta cabeçalho legível, resumo e eventos correlacionados, respeitando a ordem de cada processo.
- Incluir versão/build, Windows, arquitetura, modo portable/instalado quando identificável, formatos habilitados e configuração relevante.
- Scanner: raízes, arquivos/classes, fingerprints e motivos de cache, tempos de etapas, progresso, prazos, tentativas, campos de identidade esperados/reais, arquitetura, códigos de saída, falhas e resolução.
- Áudio/plugins: dispositivo e configuração solicitada/real, abertura/fechamento, recuperação, carregamento, buses, alterações de cadeia, erros de estado e resumos de falhas de processamento.
- Interface/IPC/persistência/atualização: ações relevantes, reconexões, resultado/duração de comandos, problemas de leitura/gravação, migração e eventos do updater quando ocorrerem dentro da captura.
- Incluir resumos periódicos de recursos e contadores, além de eventos de mudança/falha. Não gravar uma linha por sample ou bloco normal de áudio.
- Unificar os logs próprios da captura no exportador, incluindo erros existentes observáveis. Não prometer acesso a logs internos que plugins terceiros não disponibilizam.
- Não exportar áudio, conteúdo MIDI, estados binários/presets, credenciais, tokens ou dados de licença. Sanitizar argumentos/payloads e substituir o prefixo pessoal do usuário por um marcador consistente, preservando os caminhos relativos relevantes para diagnóstico.

### Escrita, limites e recuperação

- Escrita assíncrona, com buffers e filas limitados. O callback de áudio não faz I/O, espera por mutex de arquivo ou formatação/alocação de strings; publica eventos numéricos em estrutura limitada para conversão posterior.
- Reutilizar as funções existentes de log através de um backend comum, mantendo `--debug` compatível. A coleta pelo Diagnostics não abre console.
- Arquivos intermediários UTF-8 segmentados, com flush periódico e checkpoint nos limites de operações. Finalização de erro fatal deve usar um caminho mínimo, sem depender de locks potencialmente presos; registros de crash são melhores esforços, não garantia contra desligamento abrupto.
- Proposta inicial de armazenamento: segmentos de 16 MiB e teto de 256 MiB por captura, centralizados para ajuste após medição. Ao atingir o teto ou faltar espaço, pausar e avisar; preservar o que existe, registrar o motivo quando possível e permitir salvar. Não apagar eventos antigos silenciosamente.
- Se uma fila saturar, registrar contagem/lacuna e limitar repetição de eventos ruidosos; manter prioridade para transições e falhas. A captura deve declarar qualquer perda observada.
- A captura ativa sobrevive ao fechamento/reabertura e a crash por segmentos correlacionados, até o usuário encerrá-la. Na reabertura, informar interrupção e retomar a sessão ativa; uma sessão já parada para exportação permanece parada.

Aceite: eventos correlacionáveis dos três processos; ausência de I/O no callback; uso de memória/disco limitado; logs utilizáveis após encerramento inesperado; modo desligado sem trabalho detalhado residual.

## Etapa 9 — card Diagnostics, reinício e exportação

Adicionar um card no padrão atual, com descrição curta e tooltip. A coleta é controlada pelo host; o estado não depende de manter a página aberta. Comandos duplicados/reconexões não iniciam capturas extras nem reiniciam novamente o app.

| Estado | Apresentação e ações | Comportamento |
|---|---|---|
| Desativado | Toggle **Track verbose logs** desligado | Nenhuma coleta verbose |
| Aguardando reinício | Toggle ligado; **Waiting for restart**; ação de reiniciar | Preferência persistida; ainda sem coleta; desligar cancela o agendamento |
| Coletando | **Collecting verbose logs**, início, tamanho; **Stop collecting and save logs** | Host, interface e workers registram a captura |
| Parando | **Stopping collection…** | Bloquear cliques duplicados, cessar produtores, drenar buffers e fechar segmentos sem reiniciar |
| Aguardando salvamento | **Save collected logs**; captura preservada | Sem nova coleta; permitir nova tentativa após cancelamento/erro |
| Pausado por limite/erro | Explicação e ação de encerrar/salvar | Preservar captura; não indicar funcionamento normal |
| Exportado | Confirmação e toggle desligado | Persistir desativação imediata; próxima abertura não coleta |

### Modal ao ativar

Título sugerido: **Restart required**.
Texto: “Restart LightHostModern to begin collecting detailed logs. Audio processing will briefly stop during the restart.”
Botões: **Restart now** e **Restart later**, com equivalentes PT-BR.

Escolher depois mantém o estado aguardando reinício, inclusive após fechar só a janela para a bandeja. O modal aparece ao ativar, não a cada atualização de Diagnostics.

### Reinício completo

1. Persistir o agendamento e salvar normalmente preferências/estado da cadeia.
2. Usar coordenação nativa de encerramento/reabertura, reaproveitando apenas mecanismos adequados do helper existente em um modo de reinício dedicado; não executar atualização/instalação nem solicitar elevação.
3. Esperar host e interface antigos encerrarem, identificando o processo correto, e abrir a mesma instalação/perfil. Não lançar cópias concorrentes nem apenas reabrir a janela.
4. Se houver falha de salvamento/encerramento, não forçar a destruição da sessão. Explicar o erro e manter o agendamento para reinício posterior.
5. Inicializar o backend de coleta antes das operações que precisam ser investigadas e transmitir ID/caminho de sessão aos filhos.

### Encerrar e salvar

1. O botão para a coleta em todos os processos, inclusive scanner que já esteja em execução, sem parar a busca/áudio em si. O desligamento de logs nos filhos usa sinalização própria e não depende de esperar o próximo reinício.
2. Drenar buffers, finalizar o manifesto da captura e abrir o diálogo nativo Salvar como vinculado à janela WinUI.
3. Sugerir `LightHostModern-diagnostics-AAAA-MM-DD-HHMMSS.txt`, filtro `.txt` e conteúdo UTF-8.
4. Consolidar a captura em segundo plano; manter a interface responsiva. Usar escrita temporária e conclusão segura no destino, respeitando a confirmação nativa de sobrescrita.
5. Somente após a exportação completa e verificada, confirmar sucesso, persistir o toggle desligado e remover os intermediários daquela captura de forma segura. Persistir o estado exportado antes da limpeza, para não reativar coleta após crash nesse intervalo.
6. Se cancelar ou falhar, manter **Aguardando salvamento**, sem coleta, com todos os arquivos intermediários preservados e botão para tentar novamente. Não tratar cancelamento como exportação bem-sucedida.
7. Fechar o app enquanto aguarda salvamento preserva a captura; ela continua disponível e parada na próxima abertura.

A preferência que mostra/esconde Diagnostics não deve apagar ou interromper silenciosamente uma captura. Quando houver coleta ou exportação pendente, Settings deve indicar esse estado e oferecer acesso ao card, mesmo que a navegação normal de Diagnostics esteja escondida. Coleta de logs e exibição das métricas normais permanecem controles distintos.

Aceite: fluxo completo com reiniciar agora/depois, parar, cancelar, erro de escrita, nova tentativa e sucesso; nenhuma coleta antes de reiniciar; nenhuma coleta após parar; nenhuma necessidade de reiniciar para desativar; dados preservados quando não salvos.

## Etapa 10 — validação integrada, documentação e entrega local

| Área | Cenários obrigatórios |
|---|---|
| Identidade | Manifesto válido/ausente/inválido/desatualizado; pacote/binário; outra classe; vários membros; Unicode/case; sessão e aliases da 1.4.0 |
| Enumeração | Pasta produtiva >60 s; travamento real; arquivo lento; raiz ausente que volta; permissões/rede; raízes sobrepostas; links e ciclos; cancelamento |
| Cache/retry | Busca fria/quente; alteração real do conteúdo; tentativa por raiz/classe; falha resolvida; sucesso parcial; reconexão e versão de cache anterior |
| Módulos múltiplos | Classe com erro, crash e timeout após sucessos; falha da factory; retomar pendentes sem perder aceitos |
| Compatibilidade | VST2 ligado/desligado; VST3 x64; fixture x86 rejeitada claramente; efeitos mono/estéreo e metadados incompletos |
| Interface | Contadores sem inflar por retry; busca incompleta; resultados/falhas paginados; inglês/PT-BR; teclado, tooltips e layout estreito |
| Logs | Desligado; agendado; reiniciar agora/depois; início precoce; host/UI/scanner; stop com worker ativo; saturação; limite; disco cheio; crash/recuperação |
| Exportação | Caminhos Unicode/espaços; cancelar; sobrescrever; acesso negado; desconectar destino; fechar durante exportação; integridade e estado persistido após sucesso |
| Reinício | Estado da cadeia preservado; falha de salvamento; app na bandeja; perfil isolado; mesma instalação; sem elevação/cópias concorrentes |
| Desempenho | Antes/depois com logs desligados e ligados, CPU/memória/I/O/tempo por etapa, sem operações bloqueantes introduzidas no callback |

Usar primeiro fixtures próprias, executáveis de referência já disponíveis e perfis isolados. Acrescentar Xvox/Townsend/Waves reais quando houver acesso às versões corretas; não declarar essas compatibilidades verificadas sem executá-las. As antigas instruções de não rodar testes nas mudanças de layout não substituem a validação técnica desta futura correção; a execução deverá respeitar qualquer nova orientação do usuário na implementação.

Atualizar `docs/plugins.md`, `docs/diagnostics.md`, arquitetura/IPC, persistência, README quando necessário e devlog da versão a ser definida. Documentar exatamente quais eventos são coletados, limitações de crash/terceiros, localização/retomada das capturas e resultados dos testes.

Preparar build e pacotes locais somente após a implementação e as verificações pertinentes. Não publicar nem modificar releases anteriores como parte da execução deste plano.

## Ordem de execução e conclusão

1. Contratos e regressões que falham na base atual.
2. Correções de identidade, progresso e retry/cache.
3. Resultados por classe, tratamento de pastas/arquitetura e contadores.
4. Backend de logs compartilhado e instrumentação das operações corrigidas.
5. Fluxo Diagnostics/reinício/parada/exportação.
6. Otimizações guiadas por medições do fluxo final, com logs desligados e ligados.
7. Matriz integrada, documentação e entrega local.

Critério de conclusão: os três defeitos reproduzidos deixam de ocorrer; demais riscos têm comportamento explícito e regressões; resultados válidos e dados antigos são preservados; desempenho é medido; a captura segue integralmente o fluxo solicitado; pendências de plugins comerciais são declaradas separadamente. Logs não substituem correções e ausência de acesso a um plugin específico não impede corrigir defeitos já reproduzidos.
