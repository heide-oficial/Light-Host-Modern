param(
    [Parameter(Mandatory)][string]$HostExecutable,
    [ValidateSet(4,5)][int]$ProtocolVersion=5,
    [ValidateRange(2,128)][int]$Nodes=128,
    [ValidateRange(5,120)][int]$Seconds=15,
    [string]$OutputDirectory='out/canvas-resource-benchmark'
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/HostProtocol.ps1"
$repo=[IO.Path]::GetFullPath("$PSScriptRoot/..")
$root=Join-Path $repo 'out/test-profiles';$name='canvas-resource-'+[guid]::NewGuid().ToString('N');$profile=Join-Path $root $name
$script:hostProcess=$null;$uiProcess=$null;$script:hostSession=''
$hostPath=(Resolve-Path -LiteralPath $HostExecutable).Path
New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
function Request($command,$arguments=@()) {
    $r=Send-HostRequest $script:info.pipe $command $arguments -Session $script:hostSession -ProtocolVersion $ProtocolVersion
    if($r.hostSession){$script:hostSession=$r.hostSession};return $r
}
function Change($value) {
    $s=Request snapshot;$value.revision=[string]$s.chainVersion
    if($ProtocolVersion -eq 5){$value.profileId=$s.operating.activeProfile;$value.generation=$s.operating.generation}
    $r=Request operating-command @($value);$until=[DateTime]::UtcNow.AddSeconds(30)
    while($r.status -eq 'operation'){
        if($r.operationState -in @('completed','failed','cancelled')){$r=$r.result;break}
        if([DateTime]::UtcNow -gt $until){throw 'Operation timed out'}
        Start-Sleep -Milliseconds 30;$r=Request operation-status @($r.operationId,$r.hostSession)
    }
    if($r.status -ne 'ok'){throw ($r|ConvertTo-Json -Depth 10)}
}
function Start-Host {
    $script:hostProcess=Start-Process $hostPath -ArgumentList @("--test-profile=$name","--profile-root=`"$root`"") -WindowStyle Hidden -PassThru
    $until=[DateTime]::UtcNow.AddSeconds(30)
    do{try{$script:info=Get-Content (Join-Path $profile 'profile.json') -Raw|ConvertFrom-Json;if($script:info.pid -eq $script:hostProcess.Id){$s=Request snapshot;return}}catch{};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $until)
    throw 'Host startup timed out'
}
function Quit-Host {Request quit-host|Out-Null;if(!$script:hostProcess.WaitForExit(15000)){throw 'Host shutdown timed out'}}
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class CanvasBenchmarkWindow{[DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr hwnd,int command);}
'@
try{
    Start-Host;Change @{action='mode';mode='chain';discard=$true};Quit-Host;Start-Host
    Change @{action='add';kind='mixer';x=300;y=100}
    $s=Request snapshot;$graph=$s.operating.graph;$template=($graph.nodes|Where-Object kind -eq mixer)[0]|ConvertTo-Json -Depth 20
    $nodesList=@($graph.nodes|Where-Object kind -ne mixer);$edges=@();$previous='audio-in'
    for($i=0;$i -lt ($Nodes-2);$i++){
        $m=$template|ConvertFrom-Json;$m.id=[guid]::NewGuid().ToString('N');$m.x=300+($i%7)*420;$m.y=100+[Math]::Floor($i/7)*200
        $nodesList+=$m;$edges+=@{id=[guid]::NewGuid().ToString('N');from=$previous;to=$m.id;output=0;input=0;sourceWidth=2;targetWidth=2};$previous=$m.id
    }
    $edges+=@{id=[guid]::NewGuid().ToString('N');from=$previous;to='audio-out';output=0;input=0;sourceWidth=2;targetWidth=2}
    $graph.nodes=$nodesList;$graph.edges=$edges;Change @{action='graph';graph=$graph}
    [IO.File]::WriteAllText((Join-Path $profile 'ui-settings.ini'),"[Appearance]`r`nThemeMode=Dark`r`nLayoutMode=Expanded`r`nBackdropMode=1`r`n[Localization]`r`nLanguage=en-us`r`n",[Text.Encoding]::Unicode)
    $uiExe=Join-Path (Split-Path $hostPath) 'WinUI/x64/Release/LightHostModern.WinUI/LightHostModernWinUI.exe'
    $uiProcess=Start-Process $uiExe -ArgumentList @("--test-profile=$name","--profile-root=`"$root`"","--host-pipe=`"$($script:info.pipe)`"") -WindowStyle Hidden -PassThru
    $until=[DateTime]::UtcNow.AddSeconds(30)
    do{Start-Sleep -Milliseconds 200;$uiProcess.Refresh();if($uiProcess.HasExited){throw 'UI exited during startup'}}while($uiProcess.MainWindowHandle -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $until)
    if($uiProcess.MainWindowHandle -eq [IntPtr]::Zero){throw 'UI startup timed out'}
    [CanvasBenchmarkWindow]::ShowWindow($uiProcess.MainWindowHandle,9)|Out-Null
    $window=[Windows.Automation.AutomationElement]::FromHandle($uiProcess.MainWindowHandle)
    $nav=$window.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,'NavPlugins'))
    $pattern=$null
    if($nav.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)){$pattern.Invoke()}
    else{$nav.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()}
    $rows=@()
    foreach($state in @('visible','minimized','restored')){
        [CanvasBenchmarkWindow]::ShowWindow($uiProcess.MainWindowHandle,$(if($state -eq 'minimized'){6}else{9}))|Out-Null
        Start-Sleep -Seconds 3
        $uiProcess.Refresh();$script:hostProcess.Refresh();$cpu=$uiProcess.TotalProcessorTime.TotalSeconds;$hostCpu=$script:hostProcess.TotalProcessorTime.TotalSeconds
        $clock=[Diagnostics.Stopwatch]::StartNew();Start-Sleep -Seconds $Seconds;$clock.Stop()
        $uiProcess.Refresh();$script:hostProcess.Refresh()
        $rows+=@{state=$state;seconds=$clock.Elapsed.TotalSeconds;uiCpuPercent=100*($uiProcess.TotalProcessorTime.TotalSeconds-$cpu)/$clock.Elapsed.TotalSeconds/[Environment]::ProcessorCount;
            hostCpuPercent=100*($script:hostProcess.TotalProcessorTime.TotalSeconds-$hostCpu)/$clock.Elapsed.TotalSeconds/[Environment]::ProcessorCount;
            uiPrivateBytes=$uiProcess.PrivateMemorySize64;hostPrivateBytes=$script:hostProcess.PrivateMemorySize64;uiWorkingSetBytes=$uiProcess.WorkingSet64;hostWorkingSetBytes=$script:hostProcess.WorkingSet64}
    }
    @{passed=$true;profile=$profile;nodes=$Nodes;edges=$edges.Count;audioOpened=$false;hostSha256=(Get-FileHash $hostPath).Hash;uiSha256=(Get-FileHash $uiExe).Hash;
      definition='CPU normalized across all logical processors; static canvas, no audio device; not a frame-time or audio-latency benchmark';samples=$rows}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $OutputDirectory 'results.json') -Encoding UTF8
    $rows|ConvertTo-Json
}finally{
    if($uiProcess -and !$uiProcess.HasExited){$null=$uiProcess.CloseMainWindow();$null=$uiProcess.WaitForExit(6000);if(!$uiProcess.HasExited){Stop-Process -Id $uiProcess.Id -Force}}
    if($script:hostProcess -and !$script:hostProcess.HasExited){try{Quit-Host}catch{Stop-Process -Id $script:hostProcess.Id -Force}}
}
