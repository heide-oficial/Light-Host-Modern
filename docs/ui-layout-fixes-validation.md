# Ajustes de Settings e Plugins — 12/09/2026

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

Revisão da interface 1.2.2 após o feedback sobre Preferred device, abas, toolbar e hover. Complementa e substitui esses pontos do [relatório anterior](ui-toolbar-validation.md).

- **Preferred device:** seletor abaixo do título e da descrição em Compact e em larguras reduzidas. Em Expanded com espaço suficiente, a coluna do seletor fica limitada a 360 DIPs. Nomes longos usam reticências e tooltip, sem comprimir a descrição até aumentar a altura do card.
- **Abas:** Running/Installed e Scan paths/Scan plugins/Maintenance agora ficam em uma superfície de card, preservando os controles nativos SelectorBar.
- **Toolbar:** busca à esquerda e centralizada verticalmente; mute, bypass e Sort à direita em Running; Manage plugin database e Sort à direita em Installed.
- **Agrupamento:** Group by manufacturer é a primeira opção do menu Sort de Installed, seguida de separador. A preferência continua salva e a pesquisa continua filtrando os grupos.
- **Cards:** o hover pertence ao próprio card; o ListView não desenha outro retângulo por baixo. Mantidos virtualização, seleção nativa, navegação por teclado, recuo e conexões dos grupos.
- **Status:** badges imediatamente à esquerda de Add to chain/Open editor, com os ícones e cores de estado preservados.

## Verificação

Build Release x64 concluído. Cinco cenários de UI Automation passaram: toolbar de Running, Installed e agrupamento, abas/ações do banco, português e tema claro, Preferred device com os nomes longos de Windows Audio mostrados pelo usuário. A inspeção visual identificou e corrigiu o alinhamento vertical da busca; uma segunda verificação confirmou o alinhamento, o hover e o foco de teclado.

Também foi inspecionada uma janela de 1700 pixels físicos de largura, com o redimensionamento executado em contexto de DPI por monitor. Os prints confirmam que busca, ações, status e o seletor de dispositivo permanecem utilizáveis. Foram revisados Compact/Expanded, inglês/PT-BR e temas claro/escuro, a 200% de DPI. Não foram repetidos ensaios prolongados de áudio/hardware nem a instalação do MSI.

Os 18 testes CTest passaram em **12,41 segundos**. Os testes de interface usaram host simulado, perfil e pipe separados; a sessão de áudio do usuário não foi modificada.

- Cinco cenários principais (local evidence: `out/ui-layout-fixes-20260912/visual/checks/results.json`)
- Verificação final da toolbar e do foco (local evidence: `out/ui-layout-fixes-20260912/final/checks/results.json`)
- Janela de 1700 pixels físicos (local evidence: `out/ui-layout-fixes-20260912/final/narrow-checks/results.json`)

## Capturas inspecionadas

| Tela | Evidência |
| --- | --- |
| Preferred device, Compact e nome longo | Compact (local evidence: `out/ui-layout-fixes-20260912/visual/checks/preferred-device-compact-long-name.png`) |
| Preferred device, Expanded | Expanded (local evidence: `out/ui-layout-fixes-20260912/visual/checks/preferred-device-expanded-long-name.png`) |
| Toolbar e status | Running (local evidence: `out/ui-layout-fixes-20260912/final/checks/running-final.png`), Installed (local evidence: `out/ui-layout-fixes-20260912/final/checks/installed-final.png`) |
| Hover dos grupos e plugins | Fabricante (local evidence: `out/ui-layout-fixes-20260912/final/checks/manufacturer-hover-final.png`), plugin (local evidence: `out/ui-layout-fixes-20260912/final/checks/plugin-hover-final.png`) |
| Agrupamento no menu Sort | Menu (local evidence: `out/ui-layout-fixes-20260912/visual/checks/installed-sort-grouping.png`) |
| Abas do gerenciador | Modal (local evidence: `out/ui-layout-fixes-20260912/visual/checks/database-paths.png`) |
| Teclado | Foco na ação (local evidence: `out/ui-layout-fixes-20260912/final/checks/plugin-keyboard-focus.png`) |
| Janela menor | Running (local evidence: `out/ui-layout-fixes-20260912/final/narrow-checks/running-1700px.png`), Installed (local evidence: `out/ui-layout-fixes-20260912/final/narrow-checks/installed-1700px.png`) |

## Pacotes

ZIP portátil (local evidence: `out/release-ui-layout-fixes/LightHostModern-Portable.zip`) e MSI (local evidence: `out/release-ui-layout-fixes/LightHostModern-Setup.msi`), versão 1.2.2. Verificados host, UI, scanner e auxiliar de atualização. O executável, `PluginsPageView.xbf`, `SettingsPageView.xbf` e `resources.pri` do ZIP correspondem por SHA-256 ao build atual e ao staging. O MSI inclui os componentes e layouts atualizados. Fixtures e plugins de teste não foram empacotados.

Resultado da verificação dos pacotes (local evidence: `out/release-ui-layout-fixes/ui-layout-verification.json`).
