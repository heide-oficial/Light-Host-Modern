$ErrorActionPreference='Stop'
. "$PSScriptRoot/HostProtocol.ps1"
$repo=[IO.Path]::GetFullPath("$PSScriptRoot/..")
$testRoot=Join-Path $repo 'out/test-profiles'
$results=@()
foreach($stateBytes in @(4,16777216)) {
 $name='profile-digest-benchmark-'+[guid]::NewGuid().ToString('N');$directory=Join-Path $testRoot $name
 New-Item -ItemType Directory -Force -Path $directory|Out-Null
 $settings=Join-Path $directory 'LightHostModern.settings'
 & (Join-Path $repo 'out/build/windows-vs2022/Release/LightHostModernPluginInstanceTests.exe') --write-ui-fixture $settings 1
 [xml]$xml=Get-Content $settings -Raw -Encoding UTF8
 $xml.SelectSingleNode('//INSTANCE/STATE').InnerText=[string]$stateBytes+'.'+('.'*[int][Math]::Ceiling($stateBytes*8/6))
 $xml.Save($settings)
 $process=$null
 try {
  $process=Start-Process (Join-Path $repo 'out/build/windows-vs2022/LightHostModern_artefacts/Release/LightHostModern.exe') -ArgumentList @("--test-profile=$name", "--profile-root=`"$testRoot`"") -WindowStyle Hidden -PassThru
  $deadline=[DateTime]::UtcNow.AddSeconds(45)
  do {
   try {$info=Get-Content (Join-Path $directory 'profile.json') -Raw|ConvertFrom-Json;if($info.pid -eq $process.Id){$s=Send-HostRequest $info.pipe snapshot -TimeoutMs 15000;break}}catch{}
   Start-Sleep -Milliseconds 100
  }while([DateTime]::UtcNow -lt $deadline -and !$process.HasExited)
  if(!$s -or $s.hostPid -ne $process.Id){throw 'Isolated startup failed'}
  Start-Sleep -Seconds 3
  $samples=@()
  for($i=0;$i -lt 5;$i++){$watch=[Diagnostics.Stopwatch]::StartNew();$s=Send-HostRequest $info.pipe snapshot -TimeoutMs 15000;$watch.Stop();$samples+=[Math]::Round($watch.Elapsed.TotalMilliseconds,2)}
  $results+=@{stateBytes=$stateBytes;timesMs=$samples;meanMs=($samples|Measure-Object -Average).Average;hostPid=$process.Id;directory=$directory;activeCount=$s.activePlugins.Count}
  Send-HostRequest $info.pipe quit-host -Session $s.hostSession|Out-Null
  if(!$process.WaitForExit(20000)){throw 'Isolated shutdown timeout'}
 } finally {if($process -and !$process.HasExited){Stop-Process -Id $process.Id -Force}}
}
$results|ConvertTo-Json -Depth 5|Set-Content (Join-Path $repo 'out/profile-digest-benchmark-results.json') -Encoding UTF8
$results|ConvertTo-Json -Depth 5
