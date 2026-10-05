param(
    [string] $PayloadDirectory = '',
    [switch] $SimulatedAudio,
    [switch] $MeasureResources,
    [string[]] $Languages = @('en-us', 'pt-br'),
    [string[]] $Layouts = @('Compact', 'Expanded')
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\HostProtocol.ps1"
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SmokeWindow {
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern bool WritePrivateProfileString(string section, string key, string value, string file);
}
'@
$repo = (Resolve-Path "$PSScriptRoot\..").Path
if (!$PayloadDirectory) {
    $latest = Get-ChildItem -LiteralPath (Join-Path $repo 'dev-test') -Directory |
        Where-Object { $_.Name -match '^LightHostModern-build-\d+$' } |
        Sort-Object { [long]($_.Name -replace '^LightHostModern-build-', '') } -Descending |
        Select-Object -First 1
    if (!$latest) { throw 'Create a portable with Utilities/Build Dev.ps1 or pass PayloadDirectory.' }
    $PayloadDirectory = $latest.FullName
}
$payload = (Resolve-Path -LiteralPath $PayloadDirectory).Path
$root = Join-Path $repo 'out\test-profiles'
$name = 'modern-ui-' + [guid]::NewGuid().ToString('N')
$directory = Join-Path $root $name
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$arguments = @("--test-profile=$name", "--profile-root=`"$root`"")
$hostProcess = $null; $uiProcess = $null; $info = $null; $snapshot = $null
$results = [Collections.Generic.List[object]]::new()
$resourceResults = [Collections.Generic.List[object]]::new()
function Measure-App([string] $State) {
    Start-Sleep -Seconds 5
    $hostProcess.Refresh(); $uiProcess.Refresh()
    $cpuBefore = $hostProcess.TotalProcessorTime.TotalMilliseconds + $uiProcess.TotalProcessorTime.TotalMilliseconds
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $committed = 0.0
    foreach ($sample in 1..16) {
        Start-Sleep -Milliseconds 500
        $hostProcess.Refresh(); $uiProcess.Refresh()
        $committed += ($hostProcess.PrivateMemorySize64 + $uiProcess.PrivateMemorySize64) / 1048576.0
    }
    $cpuAfter = $hostProcess.TotalProcessorTime.TotalMilliseconds + $uiProcess.TotalProcessorTime.TotalMilliseconds
    $resourceResults.Add([ordered]@{ state=$State; seconds=$clock.Elapsed.TotalSeconds;
        appCpuPercent=100 * ($cpuAfter-$cpuBefore) / $clock.Elapsed.TotalMilliseconds / [Environment]::ProcessorCount;
        appCommittedMiB=$committed/16; realAudioOpened=$false })
}
function Element([string] $Id) {
    $condition = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty, $Id)
    $result = $script:window.FindFirst([Windows.Automation.TreeScope]::Descendants, $condition)
    if (!$result) { throw "Missing UI control: $Id" }
    return $result
}
function Navigate([string] $Id) {
    $element = Element $Id
    $pattern = $null
    if ($element.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern, [ref]$pattern)) { $pattern.Select() }
    elseif ($element.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern, [ref]$pattern)) { $pattern.Invoke() }
    else { throw "Cannot navigate to $Id" }
    Start-Sleep -Milliseconds 700
}
function Capture([string] $Name) {
    $uiProcess.Refresh()
    [SmokeWindow]::ShowWindow($uiProcess.MainWindowHandle, 9) | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $uiProcess.Refresh()
        if ($uiProcess.HasExited) { throw "UI exited before capture: $Name" }
        $bounds = $script:window.Current.BoundingRectangle
        $ready = ![double]::IsInfinity($bounds.Width) -and ![double]::IsNaN($bounds.Width) -and
            ![double]::IsInfinity($bounds.Height) -and ![double]::IsNaN($bounds.Height) -and
            $bounds.Width -ge 100 -and $bounds.Height -ge 100 -and $bounds.Width -le 16384 -and $bounds.Height -le 16384
        if (!$ready) {
            if ([DateTime]::UtcNow -gt $deadline) { throw "UI has no visible capture surface: $Name ($bounds)" }
            Start-Sleep -Milliseconds 100
        }
    } while (!$ready)
    $bitmap = [Drawing.Bitmap]::new([int]$bounds.Width, [int]$bounds.Height)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $dc = $graphics.GetHdc()
    try { $captured = [SmokeWindow]::PrintWindow($uiProcess.MainWindowHandle, $dc, 2) }
    finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
    try {
        if (!$captured) { throw 'Unable to capture the isolated application window.' }
        $bitmap.Save((Join-Path $directory "$Name.png"), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $bitmap.Dispose() }
}
function Choose-Grouping([string] $Id, [string] $Name) {
    $box = Element $Id
    $scrollItem = $null
    if ($box.TryGetCurrentPattern([Windows.Automation.ScrollItemPattern]::Pattern, [ref]$scrollItem)) { $scrollItem.ScrollIntoView() }
    $box.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
    Start-Sleep -Milliseconds 150
    $condition = [Windows.Automation.AndCondition]::new(
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::ListItem),
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $Name))
    $item = $box.FindFirst([Windows.Automation.TreeScope]::Descendants, $condition)
    if (!$item) { throw "Missing option in ${Id}: $Name (offscreen: $($box.Current.IsOffscreen))" }
    $item.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    $box.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Collapse()
    Start-Sleep -Milliseconds 600
}
function Wait-ChannelMask([string] $Direction, [bool[]] $Expected, [string] $Action) {
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    do {
        $state=Send-HostRequest $info.pipe 'snapshot'
        $actual=@($state.audioConfig.('active'+$Direction+'Channels'))
        $matches=$actual.Count -eq $Expected.Count
        if ($matches) {
            for ($channel=0; $channel -lt $Expected.Count; $channel++) {
                if ([bool]$actual[$channel] -ne $Expected[$channel]) { $matches=$false; break }
            }
        }
        if ($matches) { return $state }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    $details=[ordered]@{action=$Action;direction=$Direction;expected=$Expected;actual=$actual;generation=$state.audioSelection.generation}
    $details | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $directory 'channel-selection-failure.json') -Encoding UTF8
    throw "$Action ($Direction): expected [$($Expected -join ',')], received [$($actual -join ',')], generation $($state.audioSelection.generation). See channel-selection-failure.json and fixture requests.jsonl."
}
try {
    $hostProcess = Start-Process -FilePath (Join-Path $payload 'LightHostModern.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $metadata = Join-Path $directory 'profile.json'
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (!(Test-Path -LiteralPath $metadata)) {
        if ($hostProcess.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw 'Isolated host did not start.' }
        Start-Sleep -Milliseconds 100
    }
    $info = Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
    $snapshot = Send-HostRequest $info.pipe 'snapshot'
    if ($snapshot.audioSelection.processingAvailable) { throw 'Isolated profile unexpectedly opened audio.' }
    $accepted = Send-HostRequest $info.pipe 'set-diagnostics-enabled' @($true) -Session $snapshot.hostSession
    $result = Wait-HostOperation $info.pipe $accepted
    if ($result.status -ne 'ok') { throw 'Cannot enable diagnostics in isolated profile.' }
    if ($SimulatedAudio) {
        $snapshotPath = Join-Path $directory 'snapshot.json'
        $snapshot | ConvertTo-Json -Depth 60 | Set-Content $snapshotPath -Encoding UTF8
        Send-HostRequest $info.pipe 'quit-host' -Session $snapshot.hostSession | Out-Null
        if (!$hostProcess.WaitForExit(10000)) { throw 'Isolated host did not exit.' }
        $fixtureDirectory = $directory
        $hostProcess = Start-Process python -ArgumentList @("`"$PSScriptRoot\UiRedesignFakeHost.py`"", '--snapshot', "`"$snapshotPath`"", '--output', "`"$directory`"", '--modernization') -WindowStyle Hidden -PassThru
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        while (!(Test-Path (Join-Path $directory 'ready.json'))) {
            if ($hostProcess.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw 'Fixture did not start.' }
            Start-Sleep -Milliseconds 100
        }
        $info = Get-Content (Join-Path $directory 'ready.json') -Raw | ConvertFrom-Json
        $directory = $info.profile
        $arguments = @("--test-profile=$($info.name)", "--profile-root=`"$($info.root)`"")
    }
    $groupingSaved = $false
    foreach ($language in $Languages) {
        $catalog = Get-Content "$repo\WinUI\LightHostModern.WinUI\Locales\$language.json" -Raw -Encoding UTF8 | ConvertFrom-Json
        foreach ($layout in $Layouts) {
            $ini = Join-Path $directory 'ui-settings.ini'
            if (!(Test-Path $ini)) { '' | Set-Content $ini -Encoding Unicode }
            if (![SmokeWindow]::WritePrivateProfileString('Localization', 'Language', $language, $ini) -or
                ![SmokeWindow]::WritePrivateProfileString('Appearance', 'LayoutMode', $layout, $ini)) { throw 'Cannot seed isolated UI preferences.' }
            $uiArguments = $arguments + @("--host-pipe=`"$($info.pipe)`"", '--debug', "--debug-log=`"$(Join-Path $directory 'ui-debug.log')`"")
            $uiProcess = Start-Process -FilePath (Join-Path $payload 'WinUI\x64\Release\LightHostModern.WinUI\LightHostModernWinUI.exe') -ArgumentList $uiArguments -WindowStyle Hidden -PassThru
            $deadline = [DateTime]::UtcNow.AddSeconds(30)
            do {
                Start-Sleep -Milliseconds 200
                $uiProcess.Refresh()
                if ($uiProcess.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw "UI did not open for $language/$layout. See ui-debug.log." }
            } while ($uiProcess.MainWindowHandle.ToInt64() -eq 0)
            [SmokeWindow]::ShowWindow($uiProcess.MainWindowHandle, 9) | Out-Null
            $script:window = [Windows.Automation.AutomationElement]::FromHandle($uiProcess.MainWindowHandle)
            if (!$window.Current.Name.StartsWith('LightHostModern [Test:')) { throw 'UI profile isolation failed.' }
            Start-Sleep -Seconds 2
            Navigate 'NavDashboard'
            if ($MeasureResources -and !$SimulatedAudio -and $language -eq 'en-us' -and $layout -eq 'Compact') { Measure-App 'dashboard-diagnostics-enabled' }
            if ($SimulatedAudio) {
                if ((Element 'InputMeter').Current.HelpText -ne '-12.0 dBFS' -or (Element 'OutputMeter').Current.HelpText -ne '-6.0 dBFS') { throw 'Meter conversion did not match fixture amplitudes.' }
            }
            Capture "$language-$layout-dashboard"
            Navigate 'NavAudio'
            Element 'AudioBackend' | Out-Null
            # The channel card is collapsed when this no-audio profile has no channels.
            if ($SimulatedAudio) {
                $mono = Element 'InputModeBox'
                if (!$mono.Current.IsEnabled -or $mono.Current.HelpText -ne $catalog.'audio.inputMode.tooltip') { throw 'Mono control is disabled or lacks the localized explanation.' }
                if ($groupingSaved) {
                    Element 'set-input-channel-0-1' | Out-Null
                    Element 'set-output-channel-0-0' | Out-Null
                    Choose-Grouping 'InputChannelGrouping' $catalog.'audio.channelsIndividual'
                    Choose-Grouping 'OutputChannelGrouping' $catalog.'audio.channelsPairs'
                }
                foreach ($index in 0..3) { Element "set-input-channel-$index-$index" | Out-Null }
                Element 'set-output-channel-0-1' | Out-Null
                foreach ($attempt in 1..4) {
                    $before = Send-HostRequest $info.pipe 'snapshot'
                    Choose-Grouping 'InputModeBox' $(if ($before.monoInputs) { $catalog.'operating.stereo' } else { $catalog.'chain.review2.033' })
                    Start-Sleep -Seconds 1
                    $after = Send-HostRequest $info.pipe 'snapshot'
                    if ($after.monoInputs -eq $before.monoInputs) { throw 'Input mode selection did not reach the fixture or was reverted.' }
                }
                $outputMono = Element 'OutputModeBox'
                if (!$outputMono.Current.IsEnabled -or $outputMono.Current.HelpText -ne $catalog.'audio.mainOutputMode.tooltip') { throw 'Output mono lacks the localized explanation.' }
                foreach ($attempt in 1..4) {
                    $before = Send-HostRequest $info.pipe 'snapshot'
                    Choose-Grouping 'OutputModeBox' $(if ($before.monoOutput) { $catalog.'operating.stereo' } else { $catalog.'chain.review2.033' })
                    Start-Sleep -Milliseconds 700
                    $after = Send-HostRequest $info.pipe 'snapshot'
                    if ($after.monoOutput -eq $before.monoOutput -or $after.monoInputs -ne $before.monoInputs) { throw 'Output mode selection was reverted or changed input mono.' }
                }
                foreach ($direction in 'input','output') {
                    $groupId = if ($direction -eq 'input') { 'InputChannelGrouping' } else { 'OutputChannelGrouping' }
                    Choose-Grouping $groupId $catalog.'audio.channelsIndividual'
                    (Element "set-$direction-channel-0-0").GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
                    Start-Sleep -Milliseconds 700
                    $before = Send-HostRequest $info.pipe 'snapshot'
                    if ($before.audioConfig.('active'+$direction+'Channels')[0]) { throw 'Individual channel was not disabled.' }
                    Choose-Grouping $groupId $catalog.'audio.channelsPairs'
                    $after = Send-HostRequest $info.pipe 'snapshot'
                    if ($before.audioSelection.generation -ne $after.audioSelection.generation) { throw 'Changing grouping reconfigured audio.' }
                    $pair = Element "set-$direction-channel-0-1"
                    if ($pair.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Current.ToggleState -ne [Windows.Automation.ToggleState]::Indeterminate) { throw 'Partially active pair is not shown as partial.' }
                    $pair.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
                    $expectedChannels=[bool[]]@($before.audioConfig.('active'+$direction+'Channels'))
                    $expectedChannels[0]=$true; $expectedChannels[1]=$true
                    $after = Wait-ChannelMask $direction $expectedChannels 'Clicking a partial pair did not enable both channels'
                    Choose-Grouping $groupId $catalog.'audio.channelsIndividual'
                    foreach ($index in 0..1) {
                        if ((Element "set-$direction-channel-$index-$index").GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Current.ToggleState -ne [Windows.Automation.ToggleState]::On) { throw 'Grouping lost channel selections.' }
                    }
                }
                Choose-Grouping 'OutputChannelGrouping' $catalog.'audio.channelsPairs'
                if (!$groupingSaved) {
                    '{"monoDelayMs":2500}' | Set-Content (Join-Path $fixtureDirectory 'control.json') -Encoding UTF8
                    $before = Send-HostRequest $info.pipe 'snapshot'
                    Choose-Grouping 'OutputModeBox' $(if ($before.monoOutput) { $catalog.'operating.stereo' } else { $catalog.'chain.review2.033' })
                    Start-Sleep -Milliseconds 150
                    Choose-Grouping 'InputChannelGrouping' $catalog.'audio.channelsPairs'
                    Choose-Grouping 'InputChannelGrouping' $catalog.'audio.channelsIndividual'
                    Start-Sleep -Seconds 2
                    Element 'set-input-channel-0-0' | Out-Null
                    $after = Send-HostRequest $info.pipe 'snapshot'
                    if ($after.audioSelection.generation -ne $before.audioSelection.generation -or $after.monoOutput -eq $before.monoOutput) { throw 'Pending mono operation lost a presentation choice or reconfigured audio.' }
                    '{}' | Set-Content (Join-Path $fixtureDirectory 'control.json') -Encoding UTF8
                    Send-HostRequest $info.pipe 'snapshot' | Out-Null
                    Choose-Grouping 'OutputModeBox' $(if ($before.monoOutput) { $catalog.'operating.stereo' } else { $catalog.'chain.review2.033' })
                    Start-Sleep -Seconds 1
                    Choose-Grouping 'InputChannelGrouping' $catalog.'audio.channelsPairs'
                    foreach ($count in 0,1,2,3,8) {
                        @{ channelCounts=@($count,$count) } | ConvertTo-Json | Set-Content (Join-Path $fixtureDirectory 'control.json') -Encoding UTF8
                        Start-Sleep -Seconds 2
                        foreach ($direction in 'input','output') {
                            $rowCondition = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::CheckBox)
                            $rows = @($window.FindAll([Windows.Automation.TreeScope]::Descendants, $rowCondition) | Where-Object { $_.Current.AutomationId -like "set-$direction-channel-*" })
                            if ($rows.Count -ne [Math]::Ceiling($count / 2.0)) { throw "Wrong pair count for $direction with $count channels." }
                            for ($first=0; $first -lt $count; $first+=2) {
                                $last = [Math]::Min($first+1, $count-1)
                                Element "set-$direction-channel-$first-$last" | Out-Null
                            }
                            if ($count -eq 1) {
                                $expected = $catalog.('audio.'+$direction+'Channel').Replace('{0}','1')
                                if ((Element "set-$direction-channel-0-0").Current.Name -ne $expected) { throw 'Unnamed channel fallback is not localized.' }
                            }
                        }
                    }
                    '{}' | Set-Content (Join-Path $fixtureDirectory 'control.json') -Encoding UTF8
                    Start-Sleep -Seconds 2
                    Choose-Grouping 'InputChannelGrouping' $catalog.'audio.channelsIndividual'
                }
            }
            Capture "$language-$layout-audio"
            if ($SimulatedAudio) {
                $scroll = (Element 'ContentScrollViewer').GetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern)
                if ($scroll.Current.VerticallyScrollable) {
                    $scroll.SetScrollPercent(-1, 100)
                    Start-Sleep -Milliseconds 300
                    Capture "$language-$layout-audio-mono"
                    $scroll.SetScrollPercent(-1, 0)
                }
            }
            Navigate 'NavDiagnostics'
            Start-Sleep -Seconds 2
            $metrics = @($window.FindAll([Windows.Automation.TreeScope]::Descendants, [Windows.Automation.Condition]::TrueCondition) | Where-Object { $_.Current.AutomationId -like 'Diagnostic-*' })
            if ($metrics.Count -ne 21) { throw "Expected 21 diagnostic values, found $($metrics.Count)." }
            foreach ($metric in $metrics) {
                $key = $metric.Current.AutomationId.Substring('Diagnostic-'.Length)
                if ($metric.Current.HelpText -ne $catalog.('diagnostics.' + $key + '.tooltip')) { throw "Missing/incorrect $language explanation for $key" }
            }
            foreach ($id in 'appCpuPercent','appCommittedMiB') {
                if ((Element "Diagnostic-$id").Current.Name -notmatch '\d+[.]\d+') { throw "No live value for $id" }
            }
            Capture "$language-$layout-diagnostics"
            if ($MeasureResources -and !$SimulatedAudio -and $language -eq 'en-us' -and $layout -eq 'Compact') {
                Measure-App 'diagnostics-visible'
                Navigate 'NavDashboard'
                $accepted = Send-HostRequest $info.pipe 'set-diagnostics-enabled' @($false) -Session $snapshot.hostSession
                Wait-HostOperation $info.pipe $accepted | Out-Null
                Measure-App 'dashboard-diagnostics-disabled'
                $accepted = Send-HostRequest $info.pipe 'set-diagnostics-enabled' @($true) -Session $snapshot.hostSession
                Wait-HostOperation $info.pipe $accepted | Out-Null
            }
            $results.Add([ordered]@{ language=$language; layout=$layout; dpi=[SmokeWindow]::GetDpiForWindow($uiProcess.MainWindowHandle); metrics=$metrics.Count; status='passed' })
            if ($SimulatedAudio) {
                Navigate 'NavAudio'
                Choose-Grouping 'InputChannelGrouping' $catalog.'audio.channelsPairs'
                Choose-Grouping 'OutputChannelGrouping' $catalog.'audio.channelsIndividual'
                $groupingSaved = $true
                $fixtureErrors = Join-Path $fixtureDirectory 'errors.log'
                if ((Test-Path $fixtureErrors) -and (Get-Item $fixtureErrors).Length -gt 0) { throw "Fixture errors: $(Get-Content $fixtureErrors -Raw)" }
            }
            Stop-Process -Id $uiProcess.Id
            $uiProcess.WaitForExit(10000) | Out-Null
            $uiProcess = $null
        }
    }
    $results | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $directory 'ui-smoke-results.json') -Encoding UTF8
    if ($resourceResults.Count) { $resourceResults | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $directory 'resource-comparison.json') -Encoding UTF8 }
    $results | Format-Table -AutoSize
    Write-Output "Evidence: $directory"
} catch {
    if ($uiProcess -and !$uiProcess.HasExited) {
        try {
            Capture 'failure'
            $window.FindAll([Windows.Automation.TreeScope]::Descendants, [Windows.Automation.Condition]::TrueCondition) |
                ForEach-Object { $v=$_.Current; "$($v.ControlType.ProgrammaticName) | $($v.AutomationId) | $($v.Name) | offscreen=$($v.IsOffscreen)" } |
                Set-Content (Join-Path $directory 'failure-elements.txt') -Encoding UTF8
        } catch {}
    }
    throw
} finally {
    if ($uiProcess -and !$uiProcess.HasExited) { Stop-Process -Id $uiProcess.Id }
    if ($hostProcess -and !$hostProcess.HasExited) {
        if ($SimulatedAudio -and $fixtureDirectory) { New-Item -ItemType File -Path (Join-Path $fixtureDirectory 'stop') -Force | Out-Null }
        elseif ($info -and $snapshot) {
            try { Send-HostRequest $info.pipe 'quit-host' -Session $snapshot.hostSession | Out-Null } catch {}
            $hostProcess.WaitForExit(5000) | Out-Null
        }
        if (!$hostProcess.HasExited) { Stop-Process -Id $hostProcess.Id }
    }
}
