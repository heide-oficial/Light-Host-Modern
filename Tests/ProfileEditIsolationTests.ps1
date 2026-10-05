param([string]$HostExecutable='', [switch]$LegacyReproduction)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/HostProtocol.ps1"
$repo=[IO.Path]::GetFullPath("$PSScriptRoot/..")
if(!$HostExecutable){$HostExecutable=Join-Path $repo 'out/build/windows-vs2022/LightHostModern_artefacts/Release/LightHostModern.exe'}
$testRoot=Join-Path $repo 'out/test-profiles'
$name='profile-edit-isolation-'+[guid]::NewGuid().ToString('N')
$script:process=$null
$protocol=if($LegacyReproduction){4}else{5}
function Request($command,$arguments=@()) { $response=Send-HostRequest $script:info.pipe $command $arguments -ProtocolVersion $protocol -Session $script:hostSession; if($response.hostSession){$script:hostSession=$response.hostSession}; return $response }
function Start-Isolated {
 $script:hostSession=""
 $script:process=Start-Process $HostExecutable -ArgumentList @("--test-profile=$name","--profile-root=`"$testRoot`"") -WindowStyle Hidden -PassThru
 $deadline=[DateTime]::UtcNow.AddSeconds(40)
 do {
  try{$v=Get-Content (Join-Path $testRoot "$name/profile.json") -Raw|ConvertFrom-Json;if($v.pid -eq $script:process.Id){$script:info=$v;$s=Request snapshot;if($s.hostPid -eq $script:process.Id){return}}}catch{}
  Start-Sleep -Milliseconds 100
 }while([DateTime]::UtcNow -lt $deadline -and !$script:process.HasExited)
 throw 'Isolated host startup timed out'
}
function Resolve($response) {
 $deadline=[DateTime]::UtcNow.AddSeconds(15)
 while($response.status -eq 'operation'){
  if($response.operationState -in @('completed','failed','cancelled')){return $response.result}
  if([DateTime]::UtcNow -gt $deadline){throw 'Operation timed out'}
  Start-Sleep -Milliseconds 20
  $response=Request operation-status @($response.operationId,$response.hostSession)
 }
 return $response
}
function Change($request) {
 $snapshot=Request snapshot
 $request.revision=[string]$snapshot.chainVersion
 if(!$LegacyReproduction){$request.profileId=$snapshot.operating.activeProfile;$request.generation=[string]$snapshot.operating.generation}
 $result=Resolve (Request operating-command @($request))
 if($result.status -ne 'ok'){throw ($result|ConvertTo-Json -Depth 12)}
}
function Quit-Isolated {Request quit-host|Out-Null;if(!$script:process.WaitForExit(15000)){throw 'Shutdown timed out'}}
try {
 Start-Isolated
 Change @{action='mode';mode='chain';discard=$true};Quit-Isolated;Start-Isolated
 Change @{action='add';kind='mixer';x=300;y=150}
 if(!$LegacyReproduction){
  # Disk-save progress must not invalidate the revision of an unchanged graph.
  $pending=Request snapshot
  Start-Sleep -Milliseconds 1500
  $settled=Request snapshot
  if($pending.chainVersion -ne $settled.chainVersion){throw 'Autosave progress changed the graph edit revision'}
  ($pending.operating.graph.nodes|Where-Object kind -eq mixer).x=320
  $edit=@{action='graph';graph=$pending.operating.graph;revision=[string]$pending.chainVersion;profileId=$pending.operating.activeProfile;generation=$pending.operating.generation}
  if((Resolve (Request operating-command @($edit))).status -ne 'ok'){throw 'Valid edit rejected after autosave'}
  # The graph carries the revision it was based on, even if another command
  # has already produced a newer snapshot in the same profile/generation.
  foreach($structure in @('add','remove','undo')) {
   $before=Request snapshot
   if($structure -eq 'add'){Change @{action='add';kind='mixer';x=800;y=200}}
   elseif($structure -eq 'remove'){$last=@($before.operating.graph.nodes|Where-Object kind -eq mixer)[-1];Change @{action='remove';id=$last.id}}
   else{Change @{action='undo'}}
   $confirmed=Request snapshot
   $stale=@{action='graph';graph=$before.operating.graph;revision=[string]$before.chainVersion;profileId=$before.operating.activeProfile;generation=$before.operating.generation}
   if((Resolve (Request operating-command @($stale))).status -ne 'error'){throw "A stale graph was accepted after $structure"}
   $afterStale=Request snapshot
   if(($afterStale.operating.graph|ConvertTo-Json -Depth 30 -Compress) -ne ($confirmed.operating.graph|ConvertTo-Json -Depth 30 -Compress)){throw "A stale graph changed the result of $structure"}
   $id=@($confirmed.operating.graph.nodes|Where-Object kind -eq mixer)[0].id
   $preview=@{action='mixer-gain';values=@(@{id=$id;lane=0;gain=0.25});revision=[string]$before.chainVersion;profileId=$before.operating.activeProfile;generation=$before.operating.generation}
   if((Resolve (Request operating-command @($preview))).status -ne 'error'){throw "A stale gain preview was accepted after $structure"}
  }
 }
 Change @{action='create';name='Profile A';includeAudio=$false}
 $a=Request snapshot;$aId=$a.operating.activeProfile
 Change @{action='duplicate';id=$aId;name='Profile B';includeAudio=$false}
 $s=Request snapshot;$bId=($s.operating.profiles|Where-Object name -eq 'Profile B').id
 # Reproduce the previous UI: hold a graph edit, activate another profile, then
 # attach the NEW chain revision to the OLD graph (IDs intentionally overlap).
 $delayed=$a.operating.graph
 foreach($mixer in @($delayed.nodes|Where-Object kind -eq mixer)){$mixer.customName='STALE EDIT FROM A'}
 Change @{action='activate';id=$bId;discard=$true}
 $b=Request snapshot
 $request=@{action='graph';graph=$delayed;revision=[string]$b.chainVersion;profileId=$aId;generation=[string]$a.operating.generation}
 $result=Resolve (Request operating-command @($request))
 $after=Request snapshot
 $leaked=@($after.operating.graph.nodes|Where-Object {$_.kind -eq 'mixer' -and $_.customName -eq 'STALE EDIT FROM A'}).Count -gt 0
 if($LegacyReproduction){if(!$leaked){throw 'The legacy cross-profile bug was not reproduced'}}
 elseif($result.status -ne 'error' -or $leaked -or $after.operating.activeProfile -ne $bId){throw 'A stale edit crossed profile boundaries'}
 Quit-Isolated
 $report=@{legacyReproduction=[bool]$LegacyReproduction;crossProfileEditObserved=$leaked;passed=$true;profile=(Join-Path $testRoot $name)}
 $report|ConvertTo-Json|Set-Content (Join-Path $testRoot "$name/result.json") -Encoding UTF8
 $report|ConvertTo-Json
} finally {if($script:process -and !$script:process.HasExited){Stop-Process -Id $script:process.Id -Force}}
