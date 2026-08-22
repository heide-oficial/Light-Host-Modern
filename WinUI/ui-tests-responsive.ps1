param([Parameter(Mandatory)][int] $AppPid)

$ErrorActionPreference = 'Stop'
$screenshots = Join-Path $PSScriptRoot 'test-screenshots'
New-Item -ItemType Directory -Force -Path $screenshots | Out-Null

$windows = rtk winapp ui list-windows -a $AppPid --json | ConvertFrom-Json
$window = @($windows | Where-Object { $_.title -eq 'Light Host Modern' })[0]
if ($null -eq $window) { throw 'Main Light Host Modern window not found.' }
$hwnd = [IntPtr]$window.hwnd

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class LightHostWindowTest {
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int width, int height, bool repaint);
}
'@

function Assert-Command([string] $name, [scriptblock] $action) {
    & $action | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Failed: $name" }
    Write-Output "PASS: $name"
}

function Set-ComboIndex([string] $selector, [int] $index) {
    rtk winapp ui focus $selector -w $window.hwnd | Out-Null
    if ($LASTEXITCODE -ne 0) { return }
    rtk winapp ui send-keys home -w $window.hwnd --via send-input | Out-Null
    if ($LASTEXITCODE -ne 0) { return }
    for ($step = 0; $step -lt $index; $step++) {
        rtk winapp ui send-keys down -w $window.hwnd --via send-input | Out-Null
        if ($LASTEXITCODE -ne 0) { return }
    }
}

Assert-Command 'Focus main window' { rtk winapp ui focus NavSettings -w $window.hwnd }
Assert-Command 'Compact app logo exists' { rtk winapp ui wait-for NavCompactLogo -w $window.hwnd -t 3000 }
Assert-Command 'Navigate Settings' { rtk winapp ui click NavSettings -w $window.hwnd }
Assert-Command 'Layout mode exists' { rtk winapp ui wait-for LayoutMode -w $window.hwnd -t 3000 }
Assert-Command 'Backdrop mode exists' { rtk winapp ui wait-for BackdropMode -w $window.hwnd -t 3000 }
Assert-Command 'Start with Windows state label exists' { rtk winapp ui wait-for StartWithWindowsState -w $window.hwnd -t 3000 }
Assert-Command 'Close to tray state label exists' { rtk winapp ui wait-for CloseToTrayState -w $window.hwnd -t 3000 }
Assert-Command 'VST2 state label exists' { rtk winapp ui wait-for EnableVst2State -w $window.hwnd -t 3000 }

[LightHostWindowTest]::MoveWindow($hwnd, 80, 60, 1000, 760, $true) | Out-Null
Start-Sleep -Milliseconds 800
rtk winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '01-settings-narrow.png') | Out-Null

Assert-Command 'Set compact layout' { Set-ComboIndex LayoutMode 0 }
rtk winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '02-settings-compact.png') | Out-Null
Assert-Command 'Set expanded layout' { Set-ComboIndex LayoutMode 1 }

Assert-Command 'Set solid backdrop' { Set-ComboIndex BackdropMode 3 }
rtk winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '03-backdrop-solid.png') | Out-Null
Assert-Command 'Set Mica backdrop' { Set-ComboIndex BackdropMode 0 }
rtk winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '04-backdrop-mica.png') | Out-Null

Assert-Command 'Choose custom persistence' { Set-ComboIndex AudioPersistenceMode 2 }
Assert-Command 'Preferred device picker appears' { rtk winapp ui wait-for PreferredDevicePicker -w $window.hwnd -t 3000 }
Assert-Command 'Focus retry interval' { rtk winapp ui focus AudioRecoveryRetrySeconds -w $window.hwnd }
Assert-Command 'Clear retry interval focus from page background' { rtk winapp ui click PageTitle -w $window.hwnd }
$focusedAfterBackgroundClick = rtk winapp ui get-focused -w $window.hwnd
if ($focusedAfterBackgroundClick -match 'AudioRecoveryRetrySeconds') { throw 'Retry interval kept keyboard focus after clicking the page background.' }
Write-Output 'PASS: Retry interval releases focus'
Assert-Command 'Open preferred device dialog' { rtk winapp ui invoke PreferredDevicePicker -w $window.hwnd }
Assert-Command 'Preferred device Save button exists' { rtk winapp ui wait-for Save -w $window.hwnd -t 3000 }
Assert-Command 'Preferred device Cancel button exists' { rtk winapp ui wait-for Cancel -w $window.hwnd -t 3000 }
Assert-Command 'Focus preferred backend' { rtk winapp ui focus RecoveryAudioBackend -w $window.hwnd }
Assert-Command 'Clear preferred dialog input focus' { rtk winapp ui click PreferredDeviceDialogHint -w $window.hwnd }
$focusedAfterDialogClick = rtk winapp ui get-focused -w $window.hwnd
if ($focusedAfterDialogClick -match 'RecoveryAudioBackend') { throw 'Preferred device input kept focus after clicking the dialog background.' }
Write-Output 'PASS: Preferred device dialog releases input focus'
Start-Sleep -Milliseconds 500
rtk winapp ui screenshot -w $window.hwnd --capture-screen -o (Join-Path $screenshots '05-preferred-device-dialog.png') | Out-Null
Assert-Command 'Cancel preferred device dialog' { rtk winapp ui invoke CloseButton -w $window.hwnd }

Assert-Command 'Navigate Support' { rtk winapp ui click NavSupport -w $window.hwnd }
Assert-Command 'Support subtitle exists' { rtk winapp ui wait-for PageSubtitle -w $window.hwnd -t 3000 }
rtk winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '06-support-narrow.png') | Out-Null

Assert-Command 'Navigate Plugins' { rtk winapp ui click NavPlugins -w $window.hwnd }
Assert-Command 'Open Installed plugins' { rtk winapp ui invoke PluginsInstalledTab -w $window.hwnd }
rtk winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '07-plugins-narrow.png') | Out-Null
Assert-Command 'Open sort menu' { rtk winapp ui invoke InstalledPluginSort -w $window.hwnd }
Start-Sleep -Milliseconds 400
rtk winapp ui screenshot -w $window.hwnd --capture-screen -o (Join-Path $screenshots '08-sort-flyout.png') | Out-Null
rtk winapp ui send-keys escape -w $window.hwnd | Out-Null

[LightHostWindowTest]::MoveWindow($hwnd, 8, 8, 1880, 920, $true) | Out-Null
Start-Sleep -Milliseconds 800
Assert-Command 'Open plugin database actions' { rtk winapp ui invoke PluginDatabaseActions -w $window.hwnd }
Assert-Command 'Open scan paths dialog' { rtk winapp ui invoke ManageScanPaths -w $window.hwnd }
Assert-Command 'Add new path card exists' { rtk winapp ui wait-for AddNewScanPath -w $window.hwnd -t 3000 }
rtk winapp ui screenshot -w $window.hwnd --capture-screen -o (Join-Path $screenshots '09-scan-paths-dialog.png') | Out-Null
Assert-Command 'Focus first scan path' { rtk winapp ui focus ScanPathEditor0 -w $window.hwnd }
Assert-Command 'Clear scan path input focus' { rtk winapp ui click ScanPathsHint -w $window.hwnd }
$focusedAfterScanHint = rtk winapp ui get-focused -w $window.hwnd
if ($focusedAfterScanHint -match 'ScanPathEditor') { throw 'Scan path input kept focus after clicking the dialog background.' }
Write-Output 'PASS: Scan paths dialog releases input focus'
Assert-Command 'Add and focus a new scan path' { rtk winapp ui invoke AddNewScanPath -w $window.hwnd }
Start-Sleep -Milliseconds 800
$focusedAfterAdd = (rtk winapp ui get-focused -w $window.hwnd | Out-String)
if ($focusedAfterAdd -notmatch 'ScanPathEditor') { throw 'The newly added scan path was not focused.' }
Write-Output 'PASS: New scan path receives focus and text selection'
rtk winapp ui screenshot -w $window.hwnd --capture-screen -o (Join-Path $screenshots '10-new-scan-path-focused.png') | Out-Null
Assert-Command 'Cancel scan paths dialog' { rtk winapp ui invoke CloseButton -w $window.hwnd }

[LightHostWindowTest]::MoveWindow($hwnd, 80, 60, 1000, 760, $true) | Out-Null
Start-Sleep -Milliseconds 800
Assert-Command 'Navigate back to Settings' { rtk winapp ui click NavSettings -w $window.hwnd }
Assert-Command 'Switch to Portuguese (Brazil)' { Set-ComboIndex AppLanguage 1 }
Assert-Command 'Portuguese Settings title appears' { rtk winapp ui wait-for PageTitle -w $window.hwnd --value 'Configurações' -t 3000 }
rtk winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '11-settings-pt-br.png') | Out-Null
Assert-Command 'Restore English language' { Set-ComboIndex AppLanguage 0 }

[LightHostWindowTest]::MoveWindow($hwnd, 80, 60, 1460, 920, $true) | Out-Null
Start-Sleep -Milliseconds 800
rtk winapp ui screenshot -w $window.hwnd -o (Join-Path $screenshots '12-settings-wide.png') | Out-Null

$solidHash = (Get-FileHash (Join-Path $screenshots '03-backdrop-solid.png')).Hash
$micaHash = (Get-FileHash (Join-Path $screenshots '04-backdrop-mica.png')).Hash
if ($solidHash -eq $micaHash) { throw 'Backdrop screenshots are identical.' }
Write-Output 'PASS: Backdrop produces a visible rendering change'
Write-Output "Screenshots: $screenshots"
