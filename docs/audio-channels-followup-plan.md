# Plano — seleção Individual/Pares e saída principal em mono

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

> Registro das etapas locais de desenvolvimento. A preparação do release 1.4.0 e os ajustes finais de layout estão registrados em [changelog-03](../devlog/1.4.0/changelog-03.md). A autorização posterior para publicar o release substitui a restrição ao remoto destas etapas anteriores; a issue e o PR permanecem abertos.

Status: **implementado e validado localmente; pendências externas descritas abaixo**.

Este documento complementa o [plano da issue #6 e do PR #5](issue-6-implementation-plan.md), tomando como base a implementação local 1.4.0. Registra as decisões confirmadas na conversa sobre entradas e saídas e sua implementação posteriormente autorizada. A versão permanece 1.4.0 local, sem alterações no repositório remoto.

## 1. Resultado esperado na interface

Na página Audio, manter os blocos de entradas e saídas, com esta ordem interna:

| Ordem | Entradas | Saídas |
|---|---|---|
| 1 | Misturar entradas em mono | Saída principal em mono |
| 2 | Título Input channels e Marcar/Desmarcar todos | Título Output channels e Marcar/Desmarcar todos |
| 3 | Seleção: Individual / Pares | Seleção: Individual / Pares |
| 4 | Lista de canais ou pares | Lista de canais ou pares |

- Mover o controle de mono das entradas para cima do título, dentro do mesmo bloco.
- Acrescentar o controle de saída mono na posição correspondente do bloco de saídas.
- Padrões sem preferência salva: entradas em Individual; saídas em Pares; os dois modos mono desligados.
- Preservar a preferência de mono das entradas que já existe. Atualizar o aplicativo não deve desligá-la para quem já a ativou.
- Os controles mono são independentes entre si e independentes do modo de seleção dos canais.
- Traduzir títulos, opções, estados Ativado/Desativado, tooltips e nomes acessíveis em inglês/PT-BR. Nome em inglês sugerido para a nova opção: **Main output to mono**.
- Manter o comportamento responsivo: blocos lado a lado quando houver espaço e empilhados em janelas menores, com rolagem quando necessária.

## 2. Seleção Individual / Pares

### Formação e apresentação

- Individual apresenta cada canal separadamente.
- Pares reúne canais consecutivos do dispositivo: 1 + 2, 3 + 4, e assim por diante. Formar os pares a partir da lista completa, não apenas dos canais ativos.
- Uma quantidade ímpar deixa o último canal como item individual. Com um único canal, as duas apresentações são equivalentes; ocultar o seletor redundante, preservando a preferência salva.
- Mostrar os nomes informados pelo driver quando disponíveis, com fallback localizado. A identificação interna deve usar os índices reais, não o texto do rótulo.
- Um par pode estar totalmente marcado, totalmente desmarcado ou parcialmente marcado. O estado parcial significa que somente um de seus canais está ativo.

### Interação

- Trocar Individual/Pares muda apenas a apresentação e a forma de seleção conjunta. Não altera a máscara de canais, não envia uma reconfiguração de áudio e não liga/desliga mono.
- Clicar em um par desmarcado ou parcialmente marcado ativa ambos. Clicar em um par totalmente marcado desativa ambos. O estado parcial é uma indicação da seleção existente, não uma terceira escolha que o usuário precisa percorrer.
- Alterar um par em uma única operação, usando a transação de seleção de áudio já existente. Não abrir/reabrir o dispositivo duas vezes para alterar seus dois canais.
- Marcar/Desmarcar todos mantém o alcance atual: todos os canais daquele bloco, independentemente da apresentação escolhida.
- Preservar foco e seleção ao atualizar snapshots. Mudanças programáticas dos controles não podem gerar comandos como se fossem cliques.
- Se o usuário trocar a apresentação durante uma operação pendente, guardar a escolha visual e reconstruir as linhas após reconciliar a operação, sem reenviar a alteração de áudio.
- Reutilizar a validação de geração da configuração. Uma operação iniciada para outro dispositivo deve ser rejeitada e reconciliada, sem reaplicar automaticamente índices na nova lista.

## 3. Comportamento da saída principal em mono

### Qual par é afetado

O par principal corresponde aos dois primeiros canais de saída apresentados pelo dispositivo: 1 + 2, ou Left/Right quando esses forem os nomes. Essa identidade permanece fixa quando o usuário marca/desmarca canais.

| Canais principais ativos | Comportamento com saída mono habilitada |
|---|---|
| Ambos, 1 e 2 | Calcular a média dos dois sinais e enviar o mesmo resultado a ambos |
| Somente 1 ou somente 2 | Preservar o sinal e o nível desse canal, sem dividir por dois |
| Nenhum | Não aplicar mistura; as demais saídas continuam com seu fluxo atual |
| Dispositivo com apenas uma saída | Preservar o sinal e o nível; não há par para misturar |

As saídas 3, 4 e seguintes não participam dessa mistura e não recebem cópias extras dela. Por exemplo, selecionar apenas 3 + 4 não transforma esse par no principal.

JUCE compacta os canais ativos nos buffers. A implementação deve mapear explicitamente os índices físicos do par principal para suas posições no buffer. Nunca deduzir o par principal apenas de `buffer[0]` e `buffer[1]` ou da quantidade total de canais ativos.

### Mistura e posição no processamento

- Com os dois canais principais ativos, usar `M = 0,5 × L + 0,5 × R`; as duas saídas recebem M.
- Isso preserva o nível de dois sinais iguais. Sinais diferentes podem mudar de nível ao serem combinados, e sinais de polaridade oposta podem se cancelar. Não prometer volume percebido constante.
- Aplicar a transformação na saída final da cadeia, após os plugins e a escolha entre áudio processado e bypass global, antes da medição final de saída.
- Mute continua silenciando a saída. Bypass global troca para o sinal seco compensado, mas mantém a opção de escuta mono da saída.
- Não modificar os caminhos internos de bypass dos plugins para implementar essa função de saída.
- Manter o medidor de entrada antes da mistura de entrada. O medidor de saída deve representar o sinal final efetivamente enviado às saídas, incluindo a nova transformação e mute.
- Alternar estéreo/mono com transição curta de aproximadamente 5 ms, sem forçar uma passagem por silêncio. Alterações rápidas devem continuar a transição a partir do valor atual.
- Não adicionar limitador ou normalização automática. Não alterar a soma com ganho unitário já utilizada pela mistura de entradas.
- A combinação dos controles é válida: mistura nas entradas, processamento dos plugins e, opcionalmente, mistura do resultado final nas saídas principais.

### Disponibilidade

- Permitir preparar a preferência quando houver uma configuração de dispositivo conhecida, inclusive durante indisponibilidade temporária, sem abrir outro dispositivo.
- Sem dispositivo configurado, desabilitar os controles dependentes dele, seguindo o padrão atual.
- Se o par não estiver completo, preservar a preferência de saída mono e explicar no tooltip que a mistura só atua quando os dois canais principais estão ativos. Não confundir preferência habilitada com mistura efetivamente aplicada.

## 4. Estado, persistência e comunicação

- Salvar separadamente os modos de apresentação de entrada e saída nas preferências da interface, por combinação de backend/dispositivo de entrada/dispositivo de saída.
- Salvar a preferência de saída mono no host, por essa mesma combinação, como ocorre com a mistura de entrada. O host deve restaurá-la mesmo quando a interface não estiver aberta.
- Reutilizar a identificação estável de dispositivos já existente; se necessário, extrair a construção dessa identidade para um helper compartilhado, evitando formatos diferentes no host e na UI.
- Não incluir as máscaras de canais na chave de preferência: selecionar outro canal do mesmo dispositivo não deve apagar a escolha de apresentação ou mono.
- Acrescentar ao snapshot a preferência de saída mono e os dados necessários para identificar o par principal/aplicabilidade. Preferências ausentes usam os padrões deste plano.
- Acrescentar comando IPC específico de saída mono com booleano e geração esperada da configuração, seguindo o padrão de `set-mono-inputs` e a infraestrutura existente de operações.
- Evitar reenvios após timeout: reconciliar o resultado pela operação/snapshot. Impedir que uma resposta antiga restaure o estado visual de outro dispositivo.
- Preparar contagem e mapeamento das saídas fora do callback, publicando-os de forma consistente com a configuração de áudio. Nenhuma leitura de preferências, enumeração de dispositivos, alocação ou bloqueio novo no callback.
- A troca de apresentação não exige reiniciar o áudio. Ligar/desligar saída mono não exige reconstruir a cadeia nem reabrir o dispositivo.

## 5. Ordem de implementação e arquivos envolvidos

1. **Modelo de canais e persistência:** definir modos de apresentação e estados de pares; reaproveitar a identidade dos dispositivos; definir mapeamento do par principal e estado do host.
2. **Processamento de saída:** implementar média, casos com zero/uma saída principal ativa e transição suave no processador realtime. Validar o DSP antes de conectá-lo ao controle da interface.
3. **IPC e restauração:** integrar preferência, snapshot, comando com geração e restauração por dispositivo, incluindo retorno de indisponibilidade e reinício do host.
4. **Interface:** reorganizar os controles, acrescentar Individual/Pares nos dois blocos, apresentar pares parcialmente selecionados e conectar saída mono ao host.
5. **Localização e acessibilidade:** finalizar textos, tooltips, navegação por teclado, anúncio de estado parcial, foco e comportamento responsivo.
6. **Validação integrada e documentação:** executar os testes abaixo, atualizar os documentos atuais e registrar resultados e limitações no devlog da versão em desenvolvimento.

Áreas previstas:

- `Source/RealtimeHostProcessor.*`: transformação final e transição de saída.
- `Source/AudioEngine.*` e `Source/DeviceController.*`: estado, identidade, persistência e mapeamento das saídas.
- `Source/HostIpcServer.*` e `Source/IpcSchema.h`: comando e contrato de snapshot.
- `WinUI/LightHostModern.WinUI/AudioPageView.xaml`, `MainWindow.xaml.*`, `AudioPageController.h`, `PageAccessors.h` e `UiPreferences.h`: organização, seleção, sincronização e preferências visuais. Manter a lógica de canais compartilhada entre entrada e saída, sem duplicar handlers extensos.
- Catálogos `Locales/en-us.json` e `Locales/pt-br.json`, testes existentes de canais/realtime/IPC/UI e documentos `audio.md`, `audio-processing.md`, `persistence-and-recovery.md` e `architecture-and-ipc.md`.

## 6. Testes e critérios de aceite

| Área | Verificações necessárias |
|---|---|
| Apresentação | 0, 1, 2, 3 e vários canais; nomes repetidos/ausentes; pares fixos; último canal ímpar; nenhum canal alterado ao trocar Individual/Pares |
| Seleção | Estado parcial preservado nas duas direções; clique em par parcial ativa ambos; seleção conjunta em uma transação; Marcar/Desmarcar todos; falha/rollback de seleção |
| Concorrência | Troca de dispositivo e de apresentação durante operação; snapshots atrasados; geração inválida; timeout/reconexão; nenhum comando duplicado por atualização programática |
| Persistência | Reiniciar host/UI; alternar dispositivos e voltar; trocar máscaras sem perder preferências; preservar mono de entrada existente; novos campos ausentes usam padrões |
| DSP | Mono desligado preserva o sinal; L=R mantém o nível; L=-R resulta em silêncio; apenas um lado do sinal presente com o par ativo produz metade da amplitude em ambos |
| Saídas físicas | Apenas 1, apenas 2, apenas 3 + 4, 1 + 3, 2 + 4 e todas ativas; somente o par físico 1 + 2 pode ser misturado; uma saída principal ativa mantém seu nível; auxiliares permanecem inalteradas |
| Cadeia | Sem plugins, plugin mono, plugin estéreo, plugin com latência e configuração multicanal; quatro combinações dos dois controles mono; bypass por plugin/global e mute |
| Transições | Alternâncias rápidas, diferentes buffers/taxas de amostragem e parada/retorno do stream; continuidade sem fade forçado para silêncio; sem alocações do host nem bloqueios novos no callback |
| Medidores | dBFS corresponde ao resultado final; silêncio, redução por média e valores acima de 0 dBFS; funcionamento com Diagnostics desligado; barras preservadas, sem Max |
| UI | Inglês/PT-BR; Compact/Expanded; 100%, 150% e 200%; controles no topo dos blocos; rolagem, teclado, foco e estado parcial acessível |

Executar build Release do host e da UI, testes relevantes e suíte CTest. Exercitar integração em perfis isolados, sem alterar preferências de produção. Usar sinais/canais simulados para resultados determinísticos e conferir hardware real onde estiver disponível. Registrar explicitamente qualquer teste de hardware/DPI não executado.

Quando a implementação estiver validada, regenerar MSI/ZIP locais caso sejam entregues para teste, verificando que contêm a UI e o host atuais. Nenhuma etapa autoriza publicação, instalação no sistema principal ou alterações no remoto.

## 7. Limites desta etapa

O escopo inclui seleção individual ou em pares consecutivos e mono apenas no par principal de saída. Não inclui pares personalizados, cadeias independentes por entrada, nova matriz de roteamento, mono separado para cada par de saída, alteração da mistura mono de entrada ou mudanças no banco de plugins.

As validações externas pendentes do plano anterior continuam registradas naquele documento. Este plano não as considera concluídas.

## 8. Registro da implementação local

- Controles mono acima dos títulos dos respectivos blocos; Individual/Pares independente em entradas e saídas; estado parcial acessível; último canal ímpar individual; seletor oculto com zero/um canal.
- Preferências de apresentação por dispositivo no INI da interface e saída mono no host, preservando a chave existente de mono de entrada. Mudanças de máscara não trocam a identidade das preferências.
- Saída mono com média e transição de 5 ms, apenas no par físico 1 + 2, após plugins/bypass/mute. Mapeamento preparado fora do callback e medição restrita às saídas reais.
- Ambos os comandos mono validam booleano, dispositivo configurado e geração, mantendo o motivo do erro no envelope IPC. A interface reconcilia a operação e conserva a última escolha de agrupamento feita enquanto ela estava pendente.
- Textos, nomes acessíveis e tooltips em inglês/PT-BR. Documentação funcional atualizada e alterações registradas em [changelog-02](../devlog/1.4.0/changelog-02.md).

### Evidências já concluídas

- Host e WinUI compilados em Release. Suíte CTest: **20/20 aprovados**, inclusive a nova matriz de máscaras físicas `0, 1, 2, 3, 5, 10, 12, 15` a 48/96 kHz, combinações dos controles mono, plugins mono/estéreo, bypass, mute e medidores com Diagnostics desligado.
- Integração com host real em perfil isolado: ambos os comandos rejeitam geração antiga, valor não booleano e ausência de dispositivo configurado. Preferências e inicialização da instalação normal preservadas. Evidência: `out/test-profiles/integration-912db764c8aa441da703974bfedd74d3/integration-result.json`.
- MSI/ZIP regenerados em `out/release/1.4.0-local`. Inspeção aprovada de conteúdo, hashes/tamanhos, metadados e identidade de atualização. O executável WinUI do pacote possui o mesmo SHA-256 da compilação atual.
- Helper real 1.3.1 aceitou o novo MSI em modo **validate**, resultado `validated`, código 0, sem instalação. Resultado atualizado em `out/release/1.4.0-local/legacy-1.3.1-validation.json`.
- Interface do **pacote final** aprovada nos quatro cenários inglês/PT-BR × Compact/Expanded, a 192 DPI. Foram verificados os dois controles mono, seleção individual/parcial/em pares nas duas direções, ausência de reconfiguração ao mudar apresentação, restauração após reinício da UI, manutenção da última escolha durante comando mono atrasado e apresentação com 0/1/2/3/8 canais, incluindo nomes ausentes/repetidos. Capturas inspecionadas; nenhum erro do simulador registrado. Evidência: `out/test-profiles/modern-ui-d105995c86a84de7b4d6d127e7b6cf42/profiles/redesign-ad21e2cbff6a474eb8ecaa2b5903f173/ui-smoke-results.json`.

### Limites da validação

O áudio foi exercitado com sinais e dispositivos simulados nos testes automatizados; isso não substitui escuta, troca/desconexão física de dispositivos e testes com diferentes drivers/plugins reais. A interface foi exercitada em 192 DPI (200%); 96/144 DPI (100%/150%) e uma revisão completa com teclado/leitor de tela continuam pendentes. A inspeção dos pacotes não instala o app nem comprova toda a matriz de migração/rollback do Windows Installer. Nenhuma dessas pendências foi considerada concluída. Nenhuma alteração foi enviada ao remoto.
