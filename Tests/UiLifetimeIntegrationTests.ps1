param([Parameter(Mandatory)][string]$HostExecutable,
      [string]$OutputDirectory='out/ui-lifetime')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/HostProtocol.ps1"
$repo=(Resolve-Path "$PSScriptRoot/..").Path
$root=Join-Path $repo 'out/test-profiles'
$testProfileName='lifetime-'+[guid]::NewGuid().ToString('N')
$profile=Join-Path $root $testProfileName
New-Item -ItemType Directory -Force -Path $profile,$OutputDirectory | Out-Null
$results=[Collections.Generic.List[object]]::new()
$hostProcess=$null; $uiProcess=$null; $pipe=''; $session=''
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class LifetimeWindow {
  public delegate bool EnumCallback(IntPtr window, IntPtr data);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumCallback callback,IntPtr data);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder name,int count);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window,uint message,IntPtr wParam,IntPtr lParam);
  public static void OpenTray(uint process) {
    // Reopen through the real tray notification callback (NIN_BALLOONUSERCLICK).
    // This lifecycle test does not post a Windows notification or try to drive
    // the transient Quick Access popup with a synthetic mouse-down.
    EnumWindows((window,data)=> {
      uint owner; GetWindowThreadProcessId(window,out owner);
      var cls=new StringBuilder(256);GetClassName(window,cls,256);
      if(owner==process && cls.ToString().StartsWith("JUCE"))
        PostMessage(window,1124,IntPtr.Zero,new IntPtr(1029));
      return true;
    },IntPtr.Zero);
  }
}
'@
function UI([string[]]$Arguments) {
    $result=& winapp ui @Arguments -a $script:uiProcess.Id --json
    if ($LASTEXITCODE -ne 0) { throw "UI command failed: $result" }
    $result | ConvertFrom-Json
}
function Find-Ui {
    $deadline=[DateTime]::UtcNow.AddSeconds(25)
    do {
        $found=Get-CimInstance Win32_Process -Filter "Name='LightHostModernWinUI.exe'" | Where-Object { $_.CommandLine -and $_.CommandLine.Contains("--test-profile=$testProfileName") }
        if ($found) {
            if (@($found).Count -ne 1) { throw 'Duplicate UI processes for one profile.' }
            $script:uiProcess=Get-Process -Id $found.ProcessId
            $null=$script:uiProcess.Handle
            if ($script:uiProcess.MainWindowHandle -ne [IntPtr]::Zero) { break }
        }
        if ([DateTime]::UtcNow -ge $deadline) {
            Get-CimInstance Win32_Process | Where-Object {$_.Name -like '*LightHost*'} | Select-Object ProcessId,ParentProcessId,Name,CommandLine | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $OutputDirectory 'startup-processes.json') -Encoding UTF8
            throw 'Host-launched UI did not become ready.'
        }
        Start-Sleep -Milliseconds 100
    } while ($true)
    UI @('wait-for','NavDashboard','-t','10000') | Out-Null
}
function Start-Host {
    $script:hostProcess=Start-Process -FilePath $HostExecutable -ArgumentList @("--test-profile=$testProfileName","--profile-root=`"$root`"",'--show-ui') -WindowStyle Hidden -PassThru
    $null=$script:hostProcess.Handle
    $metadata=Join-Path $profile 'profile.json'; $deadline=[DateTime]::UtcNow.AddSeconds(25)
    do {
        $metadataReady=$false; $info=$null
        try { $info=Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json; $metadataReady=$info.pid -eq $hostProcess.Id -and $info.pipe } catch { }
        if ($metadataReady) { break }
        if ($hostProcess.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw 'Host startup failed.' }
        Start-Sleep -Milliseconds 100
    } while ($true)
    $script:pipe=$info.pipe
    $script:session=(Send-HostRequest $pipe 'hello').hostSession
    Find-Ui
    $snapshot=Send-HostRequest $pipe 'snapshot'
    if ($snapshot.audioSelection.processingAvailable) { throw 'Temporary profile opened audio.' }
}
function Mutate([string]$Command,[object[]]$Arguments=@()) {
    $accepted=Send-HostRequest $pipe $Command $Arguments -Session $session
    $result=Wait-HostOperation $pipe $accepted
    if ($result.status -ne 'ok') { throw ($result | ConvertTo-Json -Depth 6) }
}
function Close-Ui {
    # Exercise the native close path without a name selector that can also
    # match Settings controls such as "Close to tray".
    if (![LifetimeWindow]::PostMessage($uiProcess.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)) { throw 'Cannot request window close' }
    if (!$uiProcess.WaitForExit(10000)) { throw 'Normal UI close did not exit.' }
    if ($hostProcess.HasExited) { throw 'Close to tray incorrectly stopped the host.' }
    $script:uiProcess=$null
}
function Reopen-Ui {
    [LifetimeWindow]::OpenTray($hostProcess.Id)
    Find-Ui
}
function Sidebar([string]$Expected) {
    UI @('wait-for','BtnPaneToggle','-p','Name','--value',$Expected,'-t','5000') | Out-Null
}
function Scenario([string]$Name,[scriptblock]$Work) {
    & $Work
    $results.Add(@{name=$Name;status='passed'})
    Write-Host "PASS: $Name"
}
try {
    Start-Host
    Mutate 'set-close-behavior' @('tray')
    Scenario 'Collapsed default and normal close-to-tray' {
        Sidebar 'Expand sidebar'
        Close-Ui
        Reopen-Ui
        Sidebar 'Expand sidebar'
    }
    Scenario 'Sidebar preference saves from Appearance and applies on reopen' {
        UI @('invoke','NavSettings') | Out-Null
        UI @('scroll-into-view','SidebarOnOpen') | Out-Null
        UI @('invoke','SidebarOnOpen') | Out-Null
        $item=(UI @('search','Expanded')).matches | Where-Object { $_.type -eq 'ListItem' -and $_.name -eq 'Expanded' -and !$_.isOffscreen } | Select-Object -First 1
        if (!$item) { throw 'Expanded sidebar option not found.' }
        UI @('invoke',$item.selector) | Out-Null
        Sidebar 'Expand sidebar'
        Close-Ui
        Reopen-Ui
        Sidebar 'Collapse sidebar'
        UI @('invoke','BtnPaneToggle') | Out-Null
        Sidebar 'Expand sidebar'
        Close-Ui
        Reopen-Ui
        Sidebar 'Collapse sidebar'
    }
    Scenario 'Forced UI termination exits its audio host' {
        # The following restart also enables diagnostic evidence for the close
        # path. It remains exclusively in this test profile.
        Mutate 'set-verbose-logs' @($true)
        Stop-Process -Id $uiProcess.Id -Force
        if (!$hostProcess.WaitForExit(12000)) { throw 'Host survived forced UI termination.' }
        $script:uiProcess=$null
    }
    Scenario 'Full restart preserves Expanded and close-with-quit exits both processes' {
        Start-Host
        Sidebar 'Collapse sidebar'
        UI @('invoke','NavSettings') | Out-Null
        UI @('scroll-into-view','CloseToTray') | Out-Null
        UI @('wait-for','CloseToTray','-p','ToggleState','--value','On','-t','5000') | Out-Null
        UI @('invoke','CloseToTray') | Out-Null
        UI @('wait-for','CloseToTray','-p','ToggleState','--value','Off','-t','5000') | Out-Null
        $deadline=[DateTime]::UtcNow.AddSeconds(5)
        while((Send-HostRequest $pipe snapshot).appConfig.closeBehavior -ne 'quit'){
            if([DateTime]::UtcNow -gt $deadline){throw 'Close preference did not reach the host'}
            Start-Sleep -Milliseconds 100
        }
        if (![LifetimeWindow]::PostMessage($uiProcess.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)) { throw 'Cannot request window close' }
        $hostExited=$hostProcess.WaitForExit(12000)
        $uiExited=$uiProcess.WaitForExit(5000)
        if (!$hostExited -or !$uiExited) {
            $evidence=[ordered]@{ hostPid=$hostProcess.Id; uiPid=$uiProcess.Id; hostExited=$hostExited; uiExited=$uiExited; profile=$profile }
            try { $evidence.snapshot=Send-HostRequest $pipe snapshot -TimeoutMs 1000 } catch { $evidence.snapshotError=$_.Exception.Message }
            if (!$uiExited) {
                try { $evidence.closeToggle=UI @('search','CloseToTray'); UI @('screenshot','-o',"$OutputDirectory/close-failure.png") | Out-Null } catch { $evidence.uiError=$_.Exception.Message }
            }
            $evidence | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'close-failure.json') -Encoding UTF8
            throw "Close-with-quit left a process alive (hostExited=$hostExited, uiExited=$uiExited); see close-failure.json."
        }
    }
} catch { $results.Add(@{name='UI lifetime integration';status='failed';error=$_.Exception.Message}); throw }
finally {
    if ($hostProcess -and !$hostProcess.HasExited) {
        try { Send-HostRequest $pipe 'quit-host' -Session $session | Out-Null } catch {}
        if (!$hostProcess.WaitForExit(15000)) { Stop-Process -Id $hostProcess.Id -Force }
    }
    if ($uiProcess -and !$uiProcess.HasExited) { Stop-Process -Id $uiProcess.Id -Force }
    # Startup can fail before Find-Ui assigns the new child to uiProcess.
    Get-CimInstance Win32_Process -Filter "Name='LightHostModernWinUI.exe'" | Where-Object {
        $_.CommandLine -and $_.CommandLine.Contains("--test-profile=$testProfileName")
    } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    $results | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $OutputDirectory 'results.json') -Encoding UTF8
}
