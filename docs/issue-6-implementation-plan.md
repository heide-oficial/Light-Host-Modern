# Plano de implementação — issue #6 e integração revisada do PR #5

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

> Registro das etapas locais de desenvolvimento. A preparação do release 1.4.0 e os ajustes finais de layout estão registrados em [changelog-03](../devlog/1.4.0/changelog-03.md). A autorização posterior para publicar o release substitui a restrição ao remoto destas etapas anteriores; a issue e o PR permanecem abertos.

Data: 21/09/2026. Base examinada: código local 1.3.1.
Issue: https://github.com/heide-oficial/Light-Host-Modern/issues/6
PR: https://github.com/heide-oficial/Light-Host-Modern/pull/5

Status: implementação local em 1.4.0. As seções abaixo preservam o escopo aprovado; o registro ao final distingue a implementação das validações executadas e pendentes. Nenhuma alteração foi enviada ao remoto.

Continuação planejada, ainda sem implementação: [seleção Individual/Pares e saída principal em mono](audio-channels-followup-plan.md). Esse complemento registra a reorganização dos controles e as novas opções aprovadas após a revisão da interface.

## Decisões confirmadas

- Usar `LightHostModern` como nome canônico do produto, incluindo apresentação, pastas e identificadores próprios do projeto.
- Verificar e corrigir a migração de instalações antigas, especialmente a 1.2.0.
- Manter o nome e o comportamento de Plugin database.
- Manter os modos e os limites configuráveis de recuperação; remover a substituição automática por dispositivos padrão no modo Disabled.
- Exibir avisos em Dashboard e Audio quando o dispositivo configurado estiver indisponível e não houver processamento de áudio funcional.
- Acrescentar CPU total, RAM e explicações por tooltip em Diagnostics.
- Incluir a versão no nome do instalador, com compatibilidade para atualizadores antigos.
- Manter os medidores em barras, melhorar sua escala/resposta e mostrar o nível atual em dBFS. Não acrescentar máximo acumulado, campo Max ou ação de reset de máximo.
- Aproveitar a contribuição do PR #5 com as correções da revisão, sem integrar o código inalterado.
- Exibir os canais de entrada individualmente em Audio e oferecer **Misturar entradas em mono** como opção separada da seleção de canais. A mistura começa desligada e não cria cadeias independentes para cada entrada.

## Ordem e dependências

1. Mapear os nomes e contratos de compatibilidade e fixar a matriz de migração.
2. Padronizar o produto e implementar a migração local de dados, instalação e integração com Windows.
3. Ajustar a política de dispositivo e os avisos de áudio.
4. Integrar a seleção individual de entradas e a mistura mono, corrigindo os problemas encontrados no PR #5.
5. Ampliar Diagnostics e suas explicações.
6. Melhorar os medidores e acrescentar dBFS atual, já considerando a mistura mono.
7. Integrar o instalador versionado e a transição do atualizador.
8. Validar o conjunto, atualizar documentação e preparar a entrega.

A mudança do nome do repositório remoto deve ocorrer somente na etapa de distribuição compatível, depois de verificar o comportamento dos clientes antigos. Não é uma operação a executar durante este planejamento.

O PR de referência usa os nomes anteriores do projeto e altera arquivos também envolvidos na recuperação e nos medidores. Adaptar sua contribuição aos nomes e contratos resultantes das etapas anteriores, em uma mudança separada e revisável. Reexaminar qualquer atualização do PR posterior ao commit revisado, sem presumir que o parecer anterior cobre novos commits.

## 1. Nome canônico e compatibilidade

### Escopo

Inventariar os usos de `Light Host Modern`, `LightHost`, `LightHostWinUI`, `LightHost.WinUI` e `Light-Host-Modern`. Classificar cada ocorrência como nome atual do produto, identidade de compatibilidade, dado histórico ou referência de terceiros.

Aplicar a raiz `LightHostModern` aos nomes próprios atuais:

- Produto, janela, bandeja, atalhos, About, metadados, recursos e textos em inglês/PT-BR.
- Executável principal `LightHostModern.exe` e auxiliares com nomes de função, como `LightHostModernScanner.exe` e `LightHostModernUpdateHelper.exe`.
- Projeto/interface `LightHostModern.WinUI`, namespaces C++ e C++/WinRT, targets CMake/MSBuild e scripts de build/teste.
- Pastas de instalação, dados, logs e perfis temporários; nomes de pipes e integração de inicialização com Windows.
- Nome de repositório pretendido `LightHostModern`, links, documentação atual e automação de release.

Centralizar a identidade do produto e reduzir literais duplicados. Usar geração/validação nos formatos de build que exigem valores literais, sem criar um serviço de configuração em runtime para constantes do produto.

Não reescrever atribuições ao Light Host original, licenças, histórico Git, tags ou evidências históricas. Nomes antigos continuarão existindo onde forem necessários para detectar e migrar versões anteriores. Preservar a identidade de upgrade do MSI; estudar separadamente a identidade do pacote WinUI, que não deve mudar apenas por substituição textual.

### Migração de dados e integração

- Mapear os caminhos reais de preferências JUCE e seus arquivos irmãos de sessão, backup e temporários recuperáveis, além de banco, aliases, quarentena, posições de editores e preferências WinUI.
- Preferir dados canônicos válidos quando já existirem; importar dados legados apenas mediante uma regra explícita, sem sobrescrever uma sessão mais nova nem mesclar instâncias por aproximação.
- Preservar bytes originais e material de recuperação antes da migração; verificar o destino antes de considerar a migração concluída.
- Garantir repetição segura após interrupção, falta de espaço, arquivos bloqueados e coexistência de caminhos antigos/novos.
- Atualizar atalhos e entradas de inicialização pertencentes à instalação migrada, respeitando a preferência atual e o caminho da instalação/versão portátil.
- Revisar o launcher, a saída canônica WinUI e sua cópia para o payload, incluindo as verificações de hash existentes.

### Aceitação

Uma instalação existente abre com o novo nome e conserva ordem, UUIDs, estados independentes de plugins duplicados, dispositivos/canais, preferências de UI e configurações de inicialização. Um segundo início não repete a importação. Ocorrências remanescentes dos nomes antigos têm finalidade identificada.

## 2. Limpeza da instalação anterior

### Lacuna verificada

O MSI atual procura a instalação legada pelo `InstallLocation` no registro HKCU e contém um componente de limpeza. Os testes de inspeção não comprovam a execução dessa limpeza. O roteiro de ciclo de vida existente recebe um MSI anterior; a migração do instalador EXE da 1.2.0 requer um cenário adicional.

O código da tag 1.2.0 usa `%LOCALAPPDATA%\Programs\Light Host Modern` por padrão, enquanto o relato cita `Local\Apps`. A reprodução precisa identificar o instalador e o caminho efetivamente usados, incluindo destinos personalizados.

### Trabalho previsto

- Reproduzir com o artefato antigo real e logs de instalação em Windows descartável.
- Verificar descoberta da instalação, sequência de limpeza, elevação/contexto do usuário, arquivos em uso, atalhos e registro.
- Validar caminho e identidade do produto antes de qualquer remoção recursiva. Não remover uma pasta arbitrária apenas pelo nome ou por um caminho não validado do registro.
- Tratar pastas órfãs com identificação verificável; se não for possível identificá-las com segurança, registrar a limitação e oferecer uma orientação explícita.
- Separar remoção de binários antigos da preservação/migração de dados do usuário.
- Garantir que cancelamento ou falha não deixe a instalação anterior inutilizada sem uma instalação nova funcional ou recuperação definida.

### Matriz obrigatória

| Cenário | Resultado esperado |
| --- | --- |
| Instalador 1.2.0 → nova versão, destino padrão | Nova instalação funciona; binários, registro e atalhos antigos são tratados corretamente |
| Instalador 1.2.0 → nova versão, destino personalizado | Descoberta pelo destino real, sem apagar dados alheios |
| MSI 1.3.1 → nova versão | Upgrade reconhece o produto e migra o caminho/nome |
| App ou UI anterior em execução | Fechamento/adiamento apropriado; nenhum payload parcial apresentado como sucesso |
| Registro legado ausente | Sem remoção arbitrária; descoberta adicional comprovada ou limitação explícita |
| Cancelamento/falha e nova tentativa | Recuperação e repetição seguras |
| Reparo e desinstalação | Arquivos e atalhos coerentes; política de preservação dos dados respeitada |

Verificar hashes/conteúdo dos dados preservados, destinos dos atalhos, inventário de arquivos e identidade dos processos iniciados. Não usar a máquina principal para ensaios destrutivos de instalação.

## 3. Recuperação e avisos de áudio

### Comportamento

- Preservar os modos existentes, intervalo configurável de 1–60 segundos e limite configurável de 1–100 tentativas nos modos de persistência.
- Não introduzir retry infinito ou espera progressiva nesta mudança.
- No modo Disabled, impedir que a falha de abertura/restauração/reinício do dispositivo selecionado leve à escolha automática de outro dispositivo. Qualquer tentativa já existente de reiniciar o mesmo dispositivo deve manter a identidade exata e não criar um novo ciclo ilimitado.
- Examinar tanto a inicialização quanto o watchdog e os caminhos internos de fallback do JUCE. Não basta remover uma chamada isolada.
- Preservar o destino configurado e seus parâmetros quando ele estiver ausente; não salvar outro dispositivo como substituto.
- Manter as proteções de geração, as restrições de dispositivos, os perfis sem áudio e o estado None escolhido explicitamente.
- No primeiro uso, sem seleção anterior, orientar a configuração do áudio; não apresentar esse caso como falha de um dispositivo selecionado.

### Apresentação

Usar o mesmo estado do host para avisos em Dashboard e Audio. Texto de referência: **“O dispositivo de áudio selecionado está indisponível. O LightHostModern não está processando áudio.”**

Exibir o backend/dispositivo afetado e a causa disponível. Distinguir dispositivo ausente, erro de abertura, driver parado, bloqueio nas configurações e áudio desligado voluntariamente. O host pode continuar aberto e preservar a cadeia; o aviso não deve sugerir que todo o aplicativo travou.

Evitar mensagens que afirmem interrupção se uma configuração anterior válida ainda estiver processando após uma seleção manual rejeitada. Atualizar/remover o aviso quando a condição real mudar, sem exigir reinício da UI. Oferecer navegação para Audio a partir do Dashboard.

### Aceitação

Testes simulados devem comprovar que nenhuma criação de dispositivo alternativo ocorre após a falha, que preferências não são substituídas, que limites são respeitados e que uma escolha manual invalida tentativas antigas. Cobrir desaparecimento/retorno, suspensão/retomada, limite atingido, troca de backend, None e dispositivos bloqueados. Complementar com hardware real em perfil isolado e validar ambos os avisos na UI.

## 4. Entradas individuais e mistura mono — PR #5

### Origem e evidências da revisão

Autor da contribuição: **k-ross**. Commit revisado: `299daedb3aa7d51ac5cf96dd80548bc838b2a153`, baseado em `72a73a96107a44caf154947ec57ab3f02b700458`.

A revisão isolada confirmou que o caso de uso é válido, mas identificou correções necessárias antes da integração:

- A suíte RealtimeTests do PR passou, incluindo o teste novo. As sondas não detectaram alocações/liberações do host no processamento auditado.
- Ao ligar a opção, um sinal estável passou de 0,5 para 0,00208333 na primeira amostra. A rampa proposta reduz bruscamente o volume antes de aumentá-lo; não suaviza corretamente a troca de roteamento.
- Com um plugin de entrada/saída mono e saída estéreo do host, o resultado foi esquerda 0,5 e direita 0. A limitação vem do adaptador existente, mas precisa ser tratada para atender esse caso de uso.
- A análise do código identificou descarte de cliques durante sincronização, indicação incorreta de canais parcialmente selecionados ao voltar aos pares e tooltip que não acompanha a troca de idioma.
- Duas entradas de 0,75 produziram 1,5 com a soma em ganho unitário. Esse resultado é intencional no PR, mas exige uma política de ganho clara.

As evidências locais estão em `out/review-pr5/REVIEW.md`, `out/review-pr5/probe-results.txt` e `out/review-pr5/src/Tests/ReviewProbes.cpp`. Esses arquivos são artefatos locais ignorados pelo Git; este resumo preserva os achados essenciais no plano. A revisão recompilou o processador e os testes com objetos JUCE existentes: não equivale a build limpo completo, execução dos 18 CTests ou validação de interface/hardware.

### Interface em Audio

- Apresentar cada canal de entrada em sua própria linha, com seleção independente, mesmo quando a mistura mono estiver desligada.
- Preservar nomes, ordem e seleção real dos canais já configurados. Não habilitar ou desabilitar entradas apenas para mudar sua apresentação.
- Manter **Marcar todos/Desmarcar todos** funcionando sobre as entradas individuais e sem reconstruir indevidamente a configuração do dispositivo.
- Adicionar o interruptor **Misturar entradas em mono** / **Mix inputs to mono** dentro do card de canais de entrada.
- Ligar/desligar esse interruptor altera a mistura, sem agrupar novamente a lista nem modificar os canais selecionados. Isso elimina o caso de um par aparecer desmarcado apesar de conter uma entrada ativa.
- Explicar que todas as entradas selecionadas são combinadas em um único sinal antes da cadeia de plugins. Não apresentar a função como processamento separado de microfone e instrumento.
- Atualizar título, descrição e tooltip quando o idioma mudar, inclusive se a página já tiver sido criada. Preservar foco, uso por teclado e informações para leitor de tela.
- Manter a apresentação dos canais de saída fora desta mudança. Não acrescentar outra página nem controles novos de medição por canal.

### Mistura e transições de áudio

- Com a mistura desligada, conservar o processamento anterior. Com ela ligada, usar somente as entradas realmente habilitadas; canais não contíguos devem continuar associados às entradas físicas corretas.
- Medir a entrada antes da mistura e a saída depois do processamento e dos controles globais. Os medidores do Dashboard continuam agregados; não afirmar que a UI mostra todos os canais individualmente.
- Trocar gradualmente entre o sinal original e o sinal mono antes da cadeia, sem zerar bruscamente o ganho de toda a saída. Os caminhos usados pelos bypasses devem receber a mesma transição.
- Consumir a mudança solicitada de modo de forma coerente no callback, evitando que a opção e sua transição sejam observadas em blocos diferentes por dependerem de sinalizações separadas.
- Usar buffers preparados e operações limitadas; não acrescentar locks, alocações, logs ou alterações de preferências no callback de áudio.
- Definir a adaptação de saída mono para as saídas estéreo principais quando esse modo estiver ativo. Cobrir também um plugin mono seguido por um estéreo; preservar diferenças legítimas entre esquerda/direita produzidas por plugins estéreo.
- Não copiar indiscriminadamente o sinal para saídas auxiliares, canais de monitoramento ou todos os canais de uma configuração multicanal. Delimitar e documentar quais saídas principais recebem o mono.
- Preservar compensação de latência, bypass individual/global, mute e a continuidade da cadeia ao trocar o modo.

### Ganho e alcance da preferência

Fechar estes dois detalhes antes de codificar a integração, usando as seguintes recomendações como ponto de partida:

- **Ganho:** preservar o nível de uma única entrada e tornar explícita a soma de várias entradas. Se mantida a soma em ganho unitário do PR, explicar que o volume pode aumentar, ultrapassar 0 dBFS ou diminuir por cancelamento de fase. Não introduzir normalização automática ou limiter oculto como se isso fosse apenas uma correção. Qualquer controle adicional de ganho deve ser uma decisão explícita de escopo.
- **Persistência:** preferir uma configuração associada ao backend e ao conjunto de dispositivos selecionados, para não transportar silenciosamente uma mistura mono para outra interface com mais entradas. Definir a chave, os valores padrão e a restauração antes da implementação. Se a escolha final for uma preferência global, explicitar isso na interface e testar a troca de dispositivos.

Em qualquer modelo, a preferência pertence ao host e permanece nos snapshots; não deve existir somente na UI. O padrão é desligado onde ainda não houver escolha salva. A migração de nomes do produto deve preservar essa preferência depois que ela passar a existir.

### Comandos e sincronização

- Integrar o comando booleano ao esquema compartilhado de IPC e publicar o estado/revisão correspondente.
- Aguardar uma atualização de estado em andamento antes do primeiro envio do comando. Tratar também outra operação pendente, desconexão e ausência de snapshot válido, sem descartar silenciosamente o clique.
- Manter o estado do host como referência e mostrar falhas de aplicação. Após timeout ou reconexão, consultar/reconciliar a operação sem reenviar uma mutação já aceita.
- Preservar o foco enquanto o comando está pendente e impedir alterações conflitantes sem deixar o controle travado.
- Garantir que recriar a página ou reabrir somente a UI recupere a seleção e a opção corretas, sem iniciar novos comandos por efeito da sincronização dos controles.

### Matriz de validação

| Cenário | Resultado esperado |
| --- | --- |
| 1 entrada/2 saídas e 2 entradas/2 saídas | Seleção independente e mistura conforme o contrato escolhido |
| Entradas não contíguas, nenhuma entrada e entrada sem saída | Nenhuma troca de identidade, sinal indevido ou leitura fora dos buffers |
| Cadeia vazia, plugin mono, estéreo e sequência mono → estéreo | Saídas principais corretas; estéreo legítimo preservado |
| Buses assimétricos e configuração multicanal | Canais auxiliares não recebem cópias indevidas |
| Ligar/desligar com sinal contínuo, repetidamente e durante processamento | Sem queda abrupta de ganho; verificar as primeiras amostras, não apenas o final do bloco |
| Bypass individual/global com latência e mute | Caminhos alinhados e transições coerentes |
| Soma de várias entradas, sinais em oposição de fase e níveis acima de 0 dBFS | Ganho previsível e medidores sem ocultar o nível real |
| Clique durante snapshot, outra operação, timeout e reconexão | Ação não perdida silenciosamente nem aplicada duas vezes |
| Troca de dispositivo, reinício do host e fechamento/reabertura da UI | Preferência e seleção restauradas segundo o alcance definido |
| Troca de idioma com Audio já aberta e navegação por teclado | Tooltip atualizado, estado correto e foco preservado |

Acrescentar testes automatizados para as correções e para o comando/persistência. Executar a suíte de tempo real com auditoria, ampliar a matriz de canais/blocos e conferir que a mistura desligada não alterou o comportamento anterior. Validar a UI em inglês/PT-BR, Compact/Expanded e DPI de 100%, 150% e 200%. Completar com microfone/interface e plugins mono/estéreo reais em perfil isolado.

### Integração e crédito

Integrar a contribuição com as adaptações necessárias em uma etapa própria; não fazer merge do PR inalterado. Preservar a atribuição a k-ross no histórico e/ou registro de contribuições, de acordo com a forma de integração adotada. Registrar o resultado final no devlog, incluindo mudanças em relação à proposta original que sejam relevantes ao mantenedor.

## 5. Diagnostics: consumo e tooltips

### Informações

- CPU total do aplicativo e detalhes já existentes de host, UI e scanner, com amostragem em janelas compatíveis e percentuais normalizados para todos os processadores lógicos.
- Plugins em execução já fazem parte do host; não somar seu custo novamente. Carga DSP continua sendo uma métrica separada.
- RAM privada residente como indicador principal, com memória privada comprometida como informação complementar. Explicar as unidades e evitar somar páginas compartilhadas como se fossem consumo exclusivo.
- Definir explicitamente os processos incluídos no total, acompanhar nascimento/saída do scanner e evitar valores antigos ou zero fabricado quando uma medição não estiver disponível.
- Usar APIs compatíveis com o Windows mínimo suportado; não elevar o requisito do sistema por causa de uma API recente de memória.

Manter a coleta fora do callback de áudio e a atualização visual aproximadamente em 1 Hz enquanto Diagnostics estiver visível. Respeitar a configuração que desabilita diagnósticos e a suspensão de telemetria visual quando a janela estiver minimizada.

### Explicações

Adicionar tooltip a cada informação, com descrição curta, unidade, origem e interpretação útil. Cobrir também os indicadores existentes: DSP, CPU, xruns, falhas, MIDI descartado, formato solicitado/efetivo, latências e contadores de processamento. Disponibilizar ajuda por teclado/leitor de tela, sem depender exclusivamente do hover. Traduzir todas as mensagens para inglês/PT-BR.

### Aceitação

Validar aritmética e indisponibilidade das métricas, processos temporários, coleta desligada, janela minimizada, foco/ajuda acessível e layout em ambos os idiomas. Comparar valores com uma referência Windows usando a mesma definição de memória e janela de amostragem, sem prometer igualdade entre métricas distintas.

## 6. Medidores em barras com dBFS atual

### Comportamento

- Manter entrada e saída em barras segmentadas com as cores atuais; preservar a adaptação aos layouts Compact e Expanded.
- Substituir a escala linear de amplitude por uma escala logarítmica em dBFS. Usar inicialmente uma faixa visual de −60 a 0 dBFS, com limites de cor definidos em dBFS e verificados visualmente.
- Mostrar um valor atual ao lado de cada barra, por exemplo `−18,4 dBFS`, com largura estável e precisão de uma casa decimal.
- Usar `−∞ dBFS` para silêncio real e uma indicação de indisponibilidade quando não houver stream válido. Valores finitos abaixo da faixa podem continuar no texto; níveis acima de 0 dBFS não devem ser ocultados pelo limite da barra.
- Definir uma captura de pico por intervalo de apresentação para não perder transientes entre consultas, com ataque rápido e queda visual controlada. Isso é uma medição transitória para a barra atual, não um máximo acumulado.
- Barras e números devem representar a mesma leitura. Preservar a medição de entrada antes da mistura mono e dos plugins, e de saída após bypass/mute. A soma pode elevar a saída sem alterar o nível medido nas entradas; a documentação deve explicar essa diferença.
- Manter o canal independente dos medidores, a taxa máxima atual de apresentação e o funcionamento com Diagnostics desligado.

Não adicionar valor Max, persistência de máximo ou controles de reset. O pico retido de 1,5 segundo já existente no contrato interno não precisa ser removido nem transformado em funcionalidade de interface.

### Aceitação

Exercitar silêncio, níveis conhecidos, transientes entre consultas, sinais acima de 0 dBFS e amostras não finitas. Conferir conversão, barras/cores, texto, mute/bypass, stream parado, navegação e minimização. Verificar ausência de novas alocações/trabalho de UI no callback e legibilidade em inglês/PT-BR, Compact/Expanded e DPI de 100%, 150% e 200%.

## 7. Instalador versionado e atualização compatível

- Nome pretendido: `LightHostModern-<versão>-Setup.msi`. A próxima versão será definida antes do preparo da release, sem alterar a versão durante este planejamento.
- Atualizar descoberta/validação de assets, testes, scripts, manifesto de release, hashes e documentação. Manter validação de identidade, arquitetura, versão interna, tamanho e SHA-256.
- Publicar durante a transição uma cópia compatível com o nome `LightHostModern-Setup.msi`, de conteúdo idêntico ao instalador versionado, para clientes antigos. A cópia é uma estratégia de distribuição; não deve afrouxar a validação do atualizador.
- O nome atual do ZIP portátil já usa a raiz canônica. Não ampliar automaticamente esta mudança para versionar o ZIP.
- Coordenar a futura renomeação do repositório com uma versão de transição do atualizador. Clientes atuais validam também o prefixo exato das URLs do repositório antigo; manter só o nome antigo do MSI não resolve uma mudança de URL.
- Verificar clientes antigos reais contra os metadados/assets pretendidos. Não assumir que redirecionamentos do GitHub satisfazem a validação estrita do código antigo.
- Definir o caminho de atualização manual dos clientes que não puderem atravessar a transição. Não renomear/publicar o remoto antes de tornar esse resultado concreto e revisar a compatibilidade.

### Aceitação

O cliente novo reconhece o pacote versionado e rejeita pacotes inválidos. Os clientes antigos cobertos pela estratégia continuam encontrando a atualização. Downloads cancelados, aplicação MSI, entrega portátil e preservação da sessão continuam obedecendo ao contrato existente.

## 8. Entrega e evidências

- Organizar mudanças em etapas e commits de propósito claro, com devlogs sequenciais na pasta da versão escolhida.
- Atualizar os documentos atuais de arquitetura, áudio, recuperação, seleção individual/mistura mono, medidores, Diagnostics, build/release e atualização. Preservar o contexto dos relatórios históricos e a atribuição da contribuição integrada.
- Executar build Release do conjunto e a suíte CTest, além dos testes específicos de migração, dispositivo, mistura mono/transições, memória/CPU, medidores e atualizador afetados pelas mudanças. Não usar a aprovação dos testes originais do PR como substituto da validação das correções.
- Verificar UI real com capturas e inspeção visual, nos idiomas/layouts/DPI previstos, incluindo tooltips e avisos.
- Gerar MSI/ZIP somente após integrar o novo payload; conferir versões dos executáveis, hashes da UI e arquivos extraídos do ZIP.
- Executar o ciclo de instalação/migração em ambiente descartável e registrar o que passou, falhou ou não foi possível validar. Inspeção de pacote não substitui instalação real.
- Fazer uma medição comparativa curta e controlada do custo de Diagnostics e medidores; ampliar os ensaios se surgirem regressões ou instabilidade, sem atribuir aprovação a benchmarks históricos incompletos.
- Manter testes de execução em perfis isolados e conferir que a sessão principal não foi alterada.

Conclusão de cada etapa exige evidência correspondente. Alterações remotas, publicação e instalação não fazem parte da execução deste documento de planejamento.

## Registro da execução local — 21/09/2026

O código das etapas 1–7 foi implementado localmente. A etapa 8 possui as evidências abaixo e pendências de ambiente explícitas. O [devlog](../devlog/1.4.0/changelog-01.md) descreve as mudanças. Não foram feitos commits, push, merge, publicação ou renomeação do remoto.

### Ajustes de compatibilidade confirmados durante a implementação

- Nome canônico: `LightHostModern`; versão local: `1.4.0`.
- Preservados o registro de pacote `LightHost.WinUI`, UpgradeCode do MSI e URL do repositório existente. Uma troca textual desses contratos interromperia instalações/atualizações existentes.
- O atualizador 1.3.1 verifica também o ProductName do MSI e quatro nomes antigos de executáveis. Por isso o MSI conserva temporariamente `Light Host Modern` como ProductName, além de quatro pequenos launchers de compatibilidade. Isso também mantém esse nome com espaços na lista de programas instalados do Windows durante a transição. O app, seus processos principais e seus atalhos usam o nome canônico.
- Migração de preferências preserva os originais e publica cópias verificadas. Arquivos canônicos existentes têm prioridade inclusive quando danificados, deixando sua recuperação para o fluxo existente em vez de substituí-los silenciosamente por dados antigos.
- Limpeza do EXE antigo exige registro e binário identificáveis. Pastas órfãs, instalação de outro usuário e arquivos desconhecidos são preservados. Backups/logs ficam em `%LOCALAPPDATA%/LightHostModern/Migrations`.

### Evidência obtida

- Host e interface nativa compilados em Release. Log: `out/implementation/build-final.log` e `build-ui.log`.
- **20/20 testes CTest aprovados**, incluindo continuidade da mistura mono, cancelamento de fase, bypass, limites de criação de dispositivo, migração, medidores e atualizador. Auditoria realtime não detectou alocações do host nos cenários testados.
- `Tests/ProfileIntegrationTests.ps1` aprovado: perfil isolado, processamento de comandos, identificação de reinício, bloqueio de alterações na inicialização do Windows e ausência de abertura automática de áudio.
- MSI e ZIP gerados em `out/release/1.4.0-local`. `Tests/PackageInspectionTests.ps1` aprovado, incluindo hashes/tamanhos, alias idêntico e rejeição de UI desatualizada antes de alterar o destino.
- Executável real do helper **1.3.1** validou `LightHostModern-Setup.msi` da versão 1.4.0: resultado `validated`, código 0. O teste usou apenas `--mode validate`.
- Interface com host real validada em **inglês/PT-BR, Compact/Expanded, 192 DPI (200%)**. Os 21 indicadores expõem explicações traduzidas; CPU e memória comprometida recebem leituras reais. Capturas inspecionadas, incluindo RAM residente e avisos de áudio parado.
- Roteiro reproduzível: `Tests/ModernizationUiSmoke.ps1`, sem dependência de `winapp`/`rtk`. Usa exclusivamente perfis temporários e fecha seus próprios processos ao terminar. `-SimulatedAudio` permite verificar canais, mistura mono e valores conhecidos de dB; `-MeasureResources` mede CPU/memória com Diagnostics visível, oculto e desligado.
- Matriz simulada aprovada: quatro entradas individuais, saída estéreo em par, 16 alternâncias de mono, explicação traduzida e medidores em −12,0/−6,0 dBFS para amplitudes 0,25/0,5. Evidência: `out/test-profiles/modern-ui-0c2d756595eb4d62a8103cd1c5e0cf1f`. O teste identificou e motivou a proteção contra reenvio de mudanças programáticas do controle.
- Matriz repetida após localizar os títulos da página Audio: `out/test-profiles/modern-ui-7e0d2a9fa2114d0b85491562976d851e`, incluindo capturas com rolagem para visualizar o controle mono. Os quatro cenários passaram novamente.
- Conferência final do pacote em PT-BR/Compact aprovada após traduzir Ativado/Desativado: `out/test-profiles/modern-ui-0f7fe1635ee747c7b6e8d2248b4b136b`. O pacote final também passou novamente na inspeção e no helper 1.3.1; resultado em `out/release/1.4.0-local/legacy-1.3.1-validation.json`.
- Comparação curta, sem áudio/plugins, 5 s de aquecimento e aproximadamente 8,4 s de medição por estado: Dashboard com Diagnostics habilitado **4,21% CPU / 166,3 MiB comprometidos**; Diagnostics visível **0,22% / 176,9 MiB**; Dashboard com Diagnostics desligado **0,49% / 177,2 MiB**. Valores somam host/interface; CPU é normalizada para o computador. Amostras sequenciais incluem inicialização/carregamento de páginas, portanto **não demonstram uma diferença causal de custo nem substituem benchmark com áudio**. Arquivo: `out/test-profiles/modern-ui-1f87e51decad4a47998464f0f7f9d14a/resource-comparison.json`.

### Pendências de validação externa

- Migração real EXE 1.2.x → MSI, cancelamento/rollback, arquivos em uso, outro usuário, reparo e desinstalação em Windows descartável. A inspeção do MSI e os testes do helper **não substituem** essa matriz.
- Desconexão/reconexão física de dispositivos, diferentes drivers e escuta das transições com hardware real.
- Matriz visual em 96/144 DPI (100%/150%) e navegação completa por teclado/leitor de tela. A escala de 200% foi exercitada no ambiente disponível.
- Pacotes locais não assinados; nenhuma publicação foi realizada.
