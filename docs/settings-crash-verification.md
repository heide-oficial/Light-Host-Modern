# Crash ao abrir Settings — 12/09/2026

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

**Atualização após a reincidência:** a verificação abaixo atualizou apenas a pasta extraída. O ZIP em `release-ui-layout-fixes` continuava antigo e sua extração restaurava a UI com o crash. O ZIP e a pasta foram regenerados e verificados juntos; veja a [verificação da entrega portátil](portable-package-verification.md), que substitui essa entrega anterior.

O Windows registrou o crash das 16h17 em `out/release-ui-layout-fixes/LightHostModern-Portable/WinUI/x64/Release/LightHost.WinUI/LightHostWinUI.exe`, com acesso inválido `0xc0000005` no deslocamento `0x5d02c`. Essa cópia ainda tinha o SHA-256 `1961863775EC2371FE29E96F90E622C852139719D4DE67E66DB553772C368B2A`, anterior à correção do acesso à página Support ainda não criada.

O crash foi reproduzido ao clicar em Settings nessa cópia, em um perfil temporário com uma cópia exata do `ui-settings.ini` do usuário, incluindo `HideSupportTab=1`. O áudio foi substituído por `Tests/UiRedesignFakeHost.py` em pipe exclusivo.

A pasta da UI que estava sendo utilizada foi atualizada com os arquivos verificados de `out/release-ui-database/payload`, incluindo executável, layouts XBF, metadados e recursos. A versão comercial continua 1.2.2. Antes da substituição, foi criada a cópia recuperável `out/settings-crash-20260912/ui-before`. O host dos dois pacotes tem o mesmo hash e não foi modificado; as preferências originais também mantiveram seu hash.

A mesma cópia portátil atualizada passou no primeiro acesso a Settings e nos retornos a partir de Plugins, Database, Dashboard e Audio, usando as mesmas preferências que reproduziram o crash. Support permaneceu oculto e as capturas foram inspecionadas. Os processos de teste foram encerrados ao finalizar.

- Registro original do Windows (local evidence: `out/settings-crash-20260912/events.json`)
- Comparação dos executáveis (local evidence: `out/settings-crash-20260912/binary-comparison.json`)
- Reprodução na cópia antiga (local evidence: `out/settings-crash-20260912/visual/old-result.json`)
- Cópia atualizada e arquivos conferidos (local evidence: `out/settings-crash-20260912/updated-copy.json`)
- Resultado dos acessos a Settings (local evidence: `out/settings-crash-20260912/visual/verified/results.json`)
- Captura após a navegação (local evidence: `out/settings-crash-20260912/visual/verified/settings-after-navigation.png`)

Nenhuma nova alteração de código foi necessária nesta verificação; a correção já estava no pacote Database e foi aplicada à pasta portátil indicada pelo relatório do crash.
