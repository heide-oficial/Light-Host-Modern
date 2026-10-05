param(
    [string]$HostExecutable="$PSScriptRoot/../out/build/windows-vs2022/LightHostModern_artefacts/Release/LightHostModern.exe",
    [string]$PluginPath="$PSScriptRoot/../out/build/windows-vs2022/scn/LHFixture_artefacts/Release/VST3/LightHostModern Scenario Fixture.vst3"
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/HostProtocol.ps1"
$repo=[IO.Path]::GetFullPath("$PSScriptRoot/..")
$root=Join-Path $repo 'out/test-profiles'
$name='isolated-host-'+[guid]::NewGuid().ToString('N')
$directory=Join-Path $root $name
$script:hostProcess=$null
function Read-State { Send-HostRequest $script:info.pipe snapshot }
function Change($command,$arguments=@()) {
    $accepted=Send-HostRequest $script:info.pipe $command $arguments -Session $script:hostSession
    $result=Wait-HostOperation $script:info.pipe $accepted -TimeoutMs 45000
    if($result.status -ne 'ok'){throw ($result|ConvertTo-Json -Depth 12)}
    return $result
}
function Operating($request) {
    $s=Read-State
    $request.profileId=$s.operating.activeProfile; $request.generation=$s.operating.generation; $request.revision=[string]$s.chainVersion
    Change operating-command @($request)
}
function Wait-For($predicate,$description) {
    $until=[DateTime]::UtcNow.AddSeconds(35)
    do { if(&$predicate){return}; Start-Sleep -Milliseconds 70 }while([DateTime]::UtcNow -lt $until)
    throw "Timed out: $description"
}
function Start-Host {
    $script:hostProcess=Start-Process $HostExecutable -ArgumentList @("--test-profile=$name","--profile-root=`"$root`"") -WindowStyle Hidden -PassThru
    Wait-For {
        try { $v=Get-Content (Join-Path $directory 'profile.json') -Raw|ConvertFrom-Json
            if($v.pid -ne $script:hostProcess.Id){return $false}
            $script:info=$v; $s=Read-State; $script:hostSession=$s.hostSession; return [bool]$script:hostSession
        }catch{return $false}
    } 'host startup'
}
function Quit-Host {
    Change flush-session|Out-Null
    Send-HostRequest $script:info.pipe quit-host -Session $script:hostSession|Out-Null
    if(!$script:hostProcess.WaitForExit(15000)){throw 'Normal shutdown timed out'}
}
try {
    if(!(Test-Path -LiteralPath $PluginPath)){throw 'Build LHFixture_VST3 first'}
    Start-Host
    Change begin-plugin-scan|Out-Null
    Change scan-plugin-path @([IO.Path]::GetFullPath($PluginPath))|Out-Null
    Wait-For { !(Send-HostRequest $script:info.pipe plugin-scan-status).active } 'fixture scan'
    $known=@((Read-State).knownPluginList)[0]
    if(!$known.knownId){throw 'Fixture not discovered'}
    Change add-known-plugin @($known.knownId)|Out-Null
    $id=(Read-State).activePlugins[0].instanceId
    Change rename-plugin @($id,'Isolated test voice')|Out-Null
    Operating @{action='card-color';id=$id;color='#FF123456'}|Out-Null
    Operating @{action='isolation';id=$id;enabled=$true}|Out-Null
    Wait-For { $p=(Read-State).activePlugins[0]; $p.isolated -and $p.loading -eq 'loaded' -and $p.worker.pid -gt 0 } 'isolated load'
    $originalWorkerPid=(Read-State).activePlugins[0].worker.pid
    Operating @{action='create';name='Isolated profile';includeAudio=$false}|Out-Null
    $profileId=(Read-State).operating.activeProfile
    $duplicated=Change duplicate-plugin @($id)
    Wait-For { $s=Read-State; @($s.activePlugins|Where-Object loading -eq 'loaded').Count -eq 2 } 'isolated duplicate'
    $s=Read-State
    if($s.activePlugins[1].instanceId -ne $duplicated.instanceId -or !$s.activePlugins[1].isolated -or $s.activePlugins[1].worker.pid -eq $originalWorkerPid){throw 'Duplication did not preserve isolation with a new worker'}
    Operating @{action='overwrite';id=$profileId}|Out-Null
    Quit-Host; Start-Host
    Wait-For { @((Read-State).activePlugins|Where-Object loading -eq 'loaded').Count -eq 2 } 'restore isolated session'
    $s=Read-State
    if($s.activePlugins[0].instanceId -ne $id -or $s.activePlugins[0].name -ne 'Isolated test voice' -or $s.activePlugins[0].cardColor -ne '#FF123456'){throw 'Identity/name/color lost across worker restart'}
    # Terminate only the worker belonging to this newly-created test profile.
    $workerPid=[int]$s.activePlugins[0].worker.pid
    $workerProcess=Get-CimInstance Win32_Process -Filter "ProcessId=$workerPid"
    if($workerProcess.ParentProcessId -ne $script:hostProcess.Id){throw 'Worker ownership mismatch'}
    Stop-Process -Id $workerPid -Force
    Wait-For { (Read-State).activePlugins[0].loading -eq 'failed' } 'worker failure propagation'
    $timer=[Diagnostics.Stopwatch]::StartNew(); $s=Read-State; $timer.Stop()
    if($timer.ElapsedMilliseconds -gt 2000 -or $s.activePlugins[1].loading -ne 'loaded'){throw 'Worker failure blocked host or another instance'}
    Operating @{action='retry';id=$id}|Out-Null
    Wait-For { $p=(Read-State).activePlugins[0]; $p.loading -eq 'loaded' -and $p.worker.pid -ne $workerPid } 'manual retry'
    Operating @{action='isolation';id=$id;enabled=$false}|Out-Null
    if((Read-State).activePlugins[0].isolated){throw 'Direct execution mode not restored'}
    Quit-Host
    $saved=Get-Content (Join-Path $directory 'LightHostModern.settings.session.json') -Raw|ConvertFrom-Json
    [xml]$xml=$saved.sessionXml
    if($xml.LIGHTHOSTSESSION.INSTANCE.Count -ne 2 -or $xml.LIGHTHOSTSESSION.INSTANCE[1].isolated -ne '1'){throw 'Isolation not persisted'}
    @{passed=$true;profile=$directory;responsivenessMs=$timer.ElapsedMilliseconds;audioDeviceOpened=$false}|ConvertTo-Json|Set-Content (Join-Path $directory 'result.json') -Encoding UTF8
    Write-Output "PASS isolated host/profile/duplicate/restore/crash/retry/direct-switch: $directory"
}finally{if($script:hostProcess -and !$script:hostProcess.HasExited){Stop-Process -Id $script:hostProcess.Id -Force}}
