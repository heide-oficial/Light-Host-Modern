param(
 [string]$HostExecutable="$PSScriptRoot/../out/build/windows-vs2022/LightHostModern_artefacts/Release/LightHostModern.exe",
 [string]$PluginPath="$PSScriptRoot/../out/build/windows-vs2022/scn/LHFixture_artefacts/Release/VST3/LightHostModern Scenario Fixture.vst3",
 [string[]]$Backends=@('Windows Audio','ASIO'),
 [string]$AsioDevice='',
 [ValidateRange(0,8192)][int]$BufferSize=0,
 [ValidateRange(5,1800)][int]$Seconds=60,
 [ValidateRange(0,1)][double]$MaximumMissedBlockRatio=0.001,
 [string]$OutputDirectory='out/isolated-audio'
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/HostProtocol.ps1"
$root=[IO.Path]::GetFullPath("$PSScriptRoot/../out/test-profiles")
New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
$rows=@();$script:process=$null
function Read-State {Send-HostRequest $script:pipe snapshot}
function Change($command,$arguments=@()) {
 $r=Wait-HostOperation $script:pipe (Send-HostRequest $script:pipe $command $arguments -Session $script:hostSession) -TimeoutMs 45000
 if($r.status -ne 'ok'){throw ($r|ConvertTo-Json -Depth 8)};return $r
}
foreach($backend in $Backends){try{
 $name='isolated-audio-'+[guid]::NewGuid().ToString('N');$profile=Join-Path $root $name
 $script:process=Start-Process $HostExecutable -ArgumentList @("--test-profile=$name","--profile-root=`"$root`"") -WindowStyle Hidden -PassThru
 $deadline=[DateTime]::UtcNow.AddSeconds(30)
 while(!(Test-Path (Join-Path $profile 'profile.json'))){if($script:process.HasExited -or [DateTime]::UtcNow -gt $deadline){throw 'Host startup failed'};Start-Sleep -Milliseconds 100}
 $script:pipe=(Get-Content (Join-Path $profile 'profile.json') -Raw|ConvertFrom-Json).pipe
 $s=Read-State;$script:hostSession=$s.hostSession
 Change set-global-mute @($true)|Out-Null
 Change begin-plugin-scan|Out-Null;Change scan-plugin-path @([IO.Path]::GetFullPath($PluginPath))|Out-Null
 $deadline=[DateTime]::UtcNow.AddSeconds(45)
 while((Send-HostRequest $script:pipe plugin-scan-status).active){if([DateTime]::UtcNow -gt $deadline){throw 'Scan timed out'};Start-Sleep -Milliseconds 100}
 $known=(Read-State).knownPluginList[0];if(!$known.knownId){throw 'Fixture missing'}
 Change add-known-plugin @($known.knownId)|Out-Null
 $s=Read-State;$id=$s.activePlugins[0].instanceId
 Change operating-command @(@{action='isolation';id=$id;enabled=$true;profileId=$s.operating.activeProfile;generation=$s.operating.generation})|Out-Null
 $deadline=[DateTime]::UtcNow.AddSeconds(30)
 while((Read-State).activePlugins[0].loading -ne 'loaded'){if([DateTime]::UtcNow -gt $deadline){throw 'Worker load timed out'};Start-Sleep -Milliseconds 100}
 Change measure-callbacks @(2,$Seconds)|Out-Null
 $s=Read-State;$options=Send-HostRequest $script:pipe audio-device-options @($backend)
 if(!$options.available -or !$options.suggestedOutput){throw "Backend unavailable: $backend"}
 $device=if($backend -eq 'ASIO' -and $AsioDevice){$AsioDevice}else{[string]$options.suggestedOutput}
 if($options.outputs -notcontains $device){throw "Device is not enumerated: $device"}
 $request=@{backend=$backend;input=$(if($options.separateInputsAndOutputs){''}else{$device});output=$device;
  inputMask='0';outputMask='11';defaultInputChannels=$false;defaultOutputChannels=$false;sampleRate=48000;bufferSize=$BufferSize;expectedGeneration=$s.audioSelection.generation}
 Change select-audio-device @($request)|Out-Null
 $deadline=[DateTime]::UtcNow.AddSeconds($Seconds+25)
 do {
  Start-Sleep -Milliseconds 500;$s=Read-State
  if(!$s.globalMuted -or $s.diagnostics.inputChannels -ne 0){throw 'Output mute or disabled input changed'}
  if($s.activePlugins[0].loading -ne 'loaded'){throw ($s.activePlugins[0]|ConvertTo-Json -Depth 8)}
  $measurement=Send-HostRequest $script:pipe callback-measurement
  if($measurement.phase -eq 'interrupted' -or [DateTime]::UtcNow -gt $deadline){throw 'Measurement interrupted or timed out'}
 }while($measurement.phase -ne 'completed')
 if($s.diagnostics.processFailures -ne 0 -or $s.diagnostics.processedBlocks -le 0){throw 'Audio did not process successfully'}
 $workerBlocks=[double]$s.diagnostics.processedSamples/[Math]::Max(1,[double]$s.activePlugins[0].worker.blockSize)
 $ratio=[double]$s.activePlugins[0].worker.underruns/[Math]::Max(1,$workerBlocks)
 $rows+=@{passed=($ratio -le $MaximumMissedBlockRatio);backend=$backend;profile=$profile;outputMuted=$true;inputChannels=0;seconds=$Seconds;
  missedBlockRatio=$ratio;maximumMissedBlockRatio=$MaximumMissedBlockRatio;effective=$s.audioSelection.effective;worker=$s.activePlugins[0].worker;diagnostics=$s.diagnostics;measurement=$measurement}
 $rows|ConvertTo-Json -Depth 14|Set-Content (Join-Path $OutputDirectory 'results.json') -Encoding UTF8
 if($ratio -gt $MaximumMissedBlockRatio){throw "Isolated audio exceeded the missed-block budget: $ratio > $MaximumMissedBlockRatio. Evidence: $OutputDirectory"}
 Write-Output "PASS $backend isolated audio for $Seconds seconds; missed worker blocks: $($s.activePlugins[0].worker.underruns)"
 Change flush-session|Out-Null;Send-HostRequest $script:pipe quit-host -Session $script:hostSession|Out-Null
 if(!$script:process.WaitForExit(15000)){throw 'Host shutdown timed out'}
}finally{if($script:process -and !$script:process.HasExited){Stop-Process -Id $script:process.Id -Force}}}
