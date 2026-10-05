# Revisão da interface — 12/09/2026

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

> O desenho de Dashboard, Plugins e Diagnostics foi atualizado novamente. Consulte a [revisão de toolbars e medidores](ui-toolbar-validation.md) para o comportamento e os pacotes atuais.

Alterações implementadas em C++/WinRT e WinUI 3, mantendo a versão 1.2.2. A sessão principal continuou aberta durante o trabalho. A validação visual utilizou processos, pipes e preferências de teste separados, com dados e níveis de áudio simulados.

| Área | Resultado |
| --- | --- |
| Dashboard | Barras de entrada e saída com altura visível, escala de −60 a 0 dB e queda gradual. Removidos os textos RMS/Hold e o diálogo de canais/clipping. |
| Audio | Cards independentes para canais de entrada e saída; empilhados em espaço menor e lado a lado no layout expandido. |
| Plugins | Seletor Em execução/Instalados, ações organizadas por contexto, pesquisa e ordenação identificáveis. Cada plugin possui card próprio, ação principal e menu de ações por ID. ListView continua virtualizado. |
| Scan | Ações de scan concentradas em Instalados; progresso e falhas aparecem quando há informação relevante. |
| Scan paths | Modal limitado a 640 DIPs, campo de inclusão fixo no topo, botões de procurar/salvar, lista de caminhos com abrir/remover e gravação imediata. Valida caminhos absolutos e evita duplicatas. |
| Dispositivos permitidos | Modal limitado a 640 DIPs, seleção de backend fixa e listas de dispositivos com rolagem e nomes quebrados em linhas. |
| Diagnostics | Página própria antes de Settings, sem expander; cards com ícone, título, descrição e leituras. Atualização de 1 Hz quando a página está visível. |
| Appearance | Ícone de Theme e controladores nativos de Mica, Mica Alt e Acrylic, com ativação, tema e encerramento coordenados. Solid e alto contraste mantêm fundo sólido. |
| About | Botões Go to repo com o mesmo estilo e dimensões usados em Support. |
| Compact | Conteúdo limitado a 780 DIPs, com título e descrição no mesmo alinhamento. Rolagem horizontal desativada para evitar medições com largura ilimitada. |
| Idiomas | 450 chaves correspondentes em inglês e PT-BR. Os títulos das seções de plugins também mudam imediatamente com o idioma. |

## Verificações executadas

- Build WinUI Release x64 concluído. Permanece o aviso APPX0006 existente sobre `runFullTrust`.
- 18/18 testes CTest aprovados na regressão final, em 8,93 segundos.
- 8/8 cenários de interface aprovados: temas/materiais, alinhamento Compact, Diagnostics, About/Support, dispositivos permitidos, caminhos Unicode e duplicatas, listas/pesquisa/agrupamento, Expanded e PT-BR.
- A inspeção das capturas encontrou o atraso na tradução de Em execução/Instalados. O caso de idioma foi reforçado com asserções dos dois títulos e executado novamente após a correção: aprovado.
- Medidores verificados por UI Automation com sinais conhecidos: entrada 79,9313% e saída 89,9657%; mute levou a saída a zero, preservou a entrada e, ao desativar mute, a saída retornou a 89,9657%.
- O caminho Unicode foi salvo, recuperado em nova abertura do diálogo e removido. A gravação em perfil separado também foi observada após reabrir o processo da UI.
- A abertura do seletor nativo de pastas foi conferida. A busca de pastas não grava o caminho até acionar Salvar.
- Instâncias de teste encerradas normalmente; host e UI da sessão principal permaneceram ativos.

Evidências locais:

- Resultados dos oito cenários (local evidence: `out/ui-redesign-20260912/visual/verified/results.json`)
- Resultados dos medidores (local evidence: `out/ui-redesign-20260912/visual/verified/meters.json`)
- Reverificação da tradução (local evidence: `out/ui-redesign-20260912/visual/verified-language/results.json`)
- Log CTest (local evidence: `out/build/windows-vs2022/Testing/Temporary/LastTest.log`)

## Capturas inspecionadas

| Tela | Captura |
| --- | --- |
| Dashboard e barras | Tema escuro (local evidence: `out/ui-redesign-20260912/visual/verified/material-Mica-Alt.png`), tema claro (local evidence: `out/ui-redesign-20260912/visual/verified/light-dashboard.png`) |
| Plugins | Em execução (local evidence: `out/ui-redesign-20260912/visual/verified/running.png`), Instalados (local evidence: `out/ui-redesign-20260912/visual/verified/installed.png`), PT-BR final (local evidence: `out/ui-redesign-20260912/visual/verified-language/pt-br-running.png`) |
| Audio | Compact (local evidence: `out/ui-redesign-20260912/visual/verified/audio-compact.png`), Expanded (local evidence: `out/ui-redesign-20260912/visual/verified/audio-expanded.png`) |
| Diagnostics | Inglês (local evidence: `out/ui-redesign-20260912/visual/verified/diagnostics.png`), PT-BR (local evidence: `out/ui-redesign-20260912/visual/verified-language/pt-br-diagnostics.png`) |
| Diálogos | Scan paths (local evidence: `out/ui-redesign-20260912/visual/verified/scan-paths.png`), dispositivos permitidos (local evidence: `out/ui-redesign-20260912/visual/verified/enabled-devices.png`) |
| Settings | Appearance (local evidence: `out/ui-redesign-20260912/visual/verified/appearance.png`), About (local evidence: `out/ui-redesign-20260912/visual/verified/about.png`) |

## Limite da validação de materiais

Nesta máquina, com adaptador VMware SVGA 3D, o controlador do Windows informou `SystemBackdropState::Fallback` (1) para Mica e Mica Alt, mesmo com a janela ativa e `EnableTransparency=1`. Os logs registram `mode=0 state=1 active=1` e `mode=1 state=1 active=1`. A causa específica da decisão do sistema não foi determinada.

A configuração nativa, a alternância das opções e a continuidade da interface foram verificadas. **A diferença visual entre Mica e Mica Alt ativos não foi validada neste ambiente.** Mica Alt utiliza uma contribuição mais forte do papel de parede; não possui uma cor roxa fixa. O Windows pode substituir esses materiais por uma cor sólida conforme suas políticas de composição. Referências: [Mica e Mica Alt](https://learn.microsoft.com/en-us/windows/apps/design/style/mica), [estados do controlador](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.composition.systembackdrops.systembackdropstate?view=windows-app-sdk-1.8).

As capturas foram feitas a 200% de DPI. Esta rodada não comprova alto contraste, leitor de tela, todos os DPIs/monitores, novos testes com dispositivos reais, a matriz prolongada de desempenho nem instalação/remoção do MSI. Ela não substitui a aceitação completa do plano A–G anterior.

## Pacotes locais

- ZIP portátil (local evidence: `out/release-ui-redesign/LightHostModern-Portable.zip`)
- Instalador MSI (local evidence: `out/release-ui-redesign/LightHostModern-Setup.msi`)
- Verificação dos componentes e SHA-256 (local evidence: `out/release-ui-redesign/ui-redesign-verification.json`)

Os dois pacotes contêm host, UI, scanner e auxiliar de atualização; o ZIP também foi conferido contra a inclusão de fixtures. O SHA-256 da UI do ZIP corresponde ao executável Release atual e ao diretório de staging. O MSI informa a versão 1.2.2. O auxiliar de atualização validou nome, versão, tamanho e digest dos artefatos. Pacotes locais sem assinatura, sem publicação ou execução do instalador.

Os scripts reutilizáveis estão em `WinUI/ui-tests-redesign.ps1` e `WinUI/ui-tests-meters.ps1`. Ambos exigem o arquivo de identificação de um perfil de teste. `Tests/UiRedesignFakeHost.py` fornece o transporte e os dados simulados sem abrir dispositivos ou plugins.
