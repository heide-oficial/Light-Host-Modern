$ErrorActionPreference='Stop'
. "$PSScriptRoot/HostProtocol.ps1"
$repo=[IO.Path]::GetFullPath("$PSScriptRoot/..")
$testRoot=Join-Path $repo 'out/test-profiles'
$testName='mixer-colors-'+[guid]::NewGuid().ToString('N')
$script:hostProcess=$null
function Start-Isolated {
 $script:hostProcess=Start-Process (Join-Path $repo 'out/build/windows-vs2022/LightHostModern_artefacts/Release/LightHostModern.exe') -ArgumentList @("--test-profile=$testName", "--profile-root=`"$testRoot`"") -WindowStyle Hidden -PassThru
 $deadline=[DateTime]::UtcNow.AddSeconds(30)
 while([DateTime]::UtcNow -lt $deadline){
  $meta=Join-Path $testRoot "$testName/profile.json"
  if(Test-Path $meta){try{$v=Get-Content $meta -Raw|ConvertFrom-Json;if($v.pid -eq $script:hostProcess.Id){$script:info=$v;$script:snapshot=Send-HostRequest $info.pipe 'snapshot';return}}catch{}}
  Start-Sleep -Milliseconds 100
 }
 throw 'Host startup timed out'
}
function Change($request){
 $snapshot=Send-HostRequest $info.pipe 'snapshot'
 $request.revision=[string]$snapshot.chainVersion
 $request.profileId=$snapshot.operating.activeProfile
 $request.generation=[string]$snapshot.operating.generation
 $accepted=Send-HostRequest $info.pipe 'operating-command' @($request) -Session $snapshot.hostSession
 $result=Wait-HostOperation $info.pipe $accepted
 if($result.status -ne 'ok'){throw ($result|ConvertTo-Json -Depth 10)}
}
function Quit-Isolated {
 $snapshot=Send-HostRequest $info.pipe 'snapshot'
 Send-HostRequest $info.pipe 'quit-host' -Session $snapshot.hostSession|Out-Null
 if(!$script:hostProcess.WaitForExit(10000)){throw 'Shutdown timed out'}
}
try {
 Start-Isolated
 Change @{action='mode';mode='chain';discard=$true}
 Quit-Isolated
 Start-Isolated
 if($snapshot.operating.mode -ne 'chain'){throw 'Mode switch failed'}
 Change @{action='add';kind='mixer';x=300;y=200}
 $s=Send-HostRequest $info.pipe 'snapshot';$id=($s.operating.graph.nodes|Where-Object kind -eq mixer).id
 Change @{action='mixer-channels';id=$id;output=$false;removePair=-1}
 Change @{action='mixer-channels';id=$id;output=$true;removePair=-1}
 $s=Send-HostRequest $info.pipe 'snapshot';$mix=$s.operating.graph.nodes|Where-Object id -eq $id
 if($mix.inputs -ne 10 -or $mix.outputs -ne 4 -or $mix.gains.Count -ne 5){throw 'Dynamic mixer layout failed'}
 Change @{action='mixer-channels';id=$id;output=$false;removePair=1}
 Change @{action='undo'}
 $s=Send-HostRequest $info.pipe 'snapshot';if(($s.operating.graph.nodes|Where-Object id -eq $id).inputs -ne 10){throw 'Undo failed'}
 Change @{action='redo'}
 Quit-Isolated
 Start-Isolated
 $mix=$snapshot.operating.graph.nodes|Where-Object id -eq $id
 if($mix.inputs -ne 8 -or $mix.outputs -ne 4 -or $mix.gains.Count -ne 4){throw 'Mixer restart persistence failed'}
 Quit-Isolated
 'PASS: mixer add/remove pairs, undo/redo and restart persistence in isolated host.'
} finally {if($script:hostProcess -and !$script:hostProcess.HasExited){$script:hostProcess.Kill()}}
