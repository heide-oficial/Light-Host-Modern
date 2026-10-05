param([string]$EntryPoint='')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/HostProtocol.ps1"
$repo=[IO.Path]::GetFullPath("$PSScriptRoot/..")
$version=[regex]::Match([IO.File]::ReadAllText((Join-Path $repo 'CMakeLists.txt')),'project\(LightHostModern VERSION ([0-9]+\.[0-9]+\.[0-9]+)').Groups[1].Value
if(!$EntryPoint){$EntryPoint=Join-Path $repo 'out/build/windows-vs2022/LightHostModern_artefacts/Release/LightHostModern.exe'}
$testRoot=Join-Path $repo 'out/test-profiles'
$name='dashboard-ui-'+[guid]::NewGuid().ToString('N');$profile=Join-Path $testRoot $name
$script:session='';$hostProcess=$null;$uiProcess=$null
function Request($command,$arguments=@()){
 $r=Send-HostRequest $script:info.pipe $command $arguments -Session $script:session
 if($r.hostSession){$script:session=$r.hostSession};return $r
}
function Resolve($result){
 $until=[DateTime]::UtcNow.AddSeconds(15)
 while($result.status -eq 'operation'){
  if($result.operationState -in @('completed','failed','cancelled')){return $result.result}
  if([DateTime]::UtcNow -gt $until){throw 'IPC operation timed out'}
  Start-Sleep -Milliseconds 50;$result=Request operation-status @($result.operationId,$result.hostSession)
 };return $result
}
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
function Find([string]$key){
 $e=$script:window.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$key))
 if(!$e){throw "Control missing: $key"};return $e
}
function Read-Value([string]$key){(Find $key).Current.Name}
function Wait-Value([string]$key,[string]$expected){
 $until=[DateTime]::UtcNow.AddSeconds(15)
 do{$actual=Read-Value $key;if($actual -eq $expected){return};Start-Sleep -Milliseconds 150}while([DateTime]::UtcNow -lt $until)
 throw "$key expected '$expected', received '$actual'"
}
try{
 $launcher=Start-Process $EntryPoint -ArgumentList @("--test-profile=$name","--profile-root=`"$testRoot`"") -WindowStyle Hidden -PassThru
 $until=[DateTime]::UtcNow.AddSeconds(25)
 do{try{$script:info=Get-Content (Join-Path $profile 'profile.json') -Raw|ConvertFrom-Json;$snapshot=Request snapshot;if($snapshot.hostPid -eq $script:info.pid){break}}catch{};Start-Sleep -Milliseconds 150}while([DateTime]::UtcNow -lt $until)
 if(!$snapshot -or $snapshot.hostPid -ne $script:info.pid){throw 'Isolated host did not start'}
 $hostProcess=Get-Process -Id $script:info.pid
 $hostPath=(Get-CimInstance Win32_Process -Filter "ProcessId=$($hostProcess.Id)").ExecutablePath
 [IO.File]::WriteAllText((Join-Path $profile 'ui-settings.ini'),"[Appearance]`r`nThemeMode=Dark`r`nLayoutMode=Expanded`r`nBackdropMode=3`r`n[Localization]`r`nLanguage=en-us`r`n",[Text.Encoding]::Unicode)
 $uiExe=Join-Path (Split-Path $hostPath) 'WinUI/x64/Release/LightHostModern.WinUI/LightHostModernWinUI.exe'
 $uiProcess=Start-Process $uiExe -ArgumentList @("--test-profile=$name","--profile-root=`"$testRoot`"","--host-pipe=`"$($script:info.pipe)`"") -WindowStyle Hidden -PassThru
 $until=[DateTime]::UtcNow.AddSeconds(25)
 do{Start-Sleep -Milliseconds 150;$uiProcess.Refresh()}while(!$uiProcess.MainWindowTitle.Contains('[Test:') -and [DateTime]::UtcNow -lt $until)
 if(!$uiProcess.MainWindowTitle.Contains('[Test:')){throw 'Isolated UI did not start'}
 $script:window=[Windows.Automation.AutomationElement]::FromHandle($uiProcess.MainWindowHandle)
 $captures=Join-Path $profile 'captures'
 & "$repo/Utilities/Capture UI Review.ps1" -UiPid $uiProcess.Id -Width 1800 -Height 1550 -OutputDirectory $captures -Name dashboard-initial -Passive
 # Scroll using UIA rather than moving the user's mouse or changing keyboard focus.
 $scroll=Find ContentScrollViewer
 $scroll.GetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern).SetScrollPercent(-1,100)
 Start-Sleep -Milliseconds 1500
 Wait-Value DashboardActiveProfile Default
 Wait-Value DashboardAppVersion ('v'+$version)
 $until=[DateTime]::UtcNow.AddSeconds(15)
 do{$cpu=Read-Value DashboardCpuUsage;$ram=Read-Value DashboardRamUsage;$vram=Read-Value DashboardVramUsage;if($cpu -match '^\d+\.\d%$' -and $ram -ne '--' -and $vram -ne '--'){break};Start-Sleep -Milliseconds 200}while([DateTime]::UtcNow -lt $until)
 if($cpu -notmatch '^\d+\.\d%$'){throw "CPU did not produce a sampled reading: $cpu"}
 foreach($value in @($ram,$vram)){if($value -ne 'Unavailable' -and $value -notmatch '^\d+\.\d MiB$'){throw "Invalid memory reading: $value"}}
 $values=@{cpu=$cpu;ram=$ram;vram=$vram}
 $state=Request snapshot
 $request=@{action='create';name='Dashboard profile';includeAudio=$false;revision=[string]$state.chainVersion;profileId=$state.operating.activeProfile;generation=[string]$state.operating.generation}
 if((Resolve (Request operating-command @($request))).status -ne 'ok'){throw 'Could not create isolated test profile'}
 Wait-Value DashboardActiveProfile 'Dashboard profile'
 & "$repo/Utilities/Capture UI Review.ps1" -UiPid $uiProcess.Id -Name dashboard-resources -OutputDirectory $captures -Passive
 # Disabling diagnostics must clear live readings rather than leave stale numbers.
 if((Resolve (Request set-diagnostics-enabled @($false))).status -ne 'ok'){throw 'Could not disable diagnostics'}
 foreach($key in @('DashboardCpuUsage','DashboardRamUsage','DashboardVramUsage')){Wait-Value $key Disabled}
 if((Resolve (Request set-diagnostics-enabled @($true))).status -ne 'ok'){throw 'Could not re-enable diagnostics'}
 $until=[DateTime]::UtcNow.AddSeconds(15)
 do{$cpu=Read-Value DashboardCpuUsage;if($cpu -match '^\d+\.\d%$'){break};Start-Sleep -Milliseconds 150}while([DateTime]::UtcNow -lt $until)
 if($cpu -notmatch '^\d+\.\d%$'){throw 'CPU did not resume after diagnostics was enabled'}
 $result=@{passed=$true;profile=$profile;readings=$values;profileRefresh=$true;diagnosticsToggle=$true;version=$true}
 $result|ConvertTo-Json|Set-Content (Join-Path $profile 'dashboard-result.json') -Encoding UTF8
 $result|ConvertTo-Json
}finally{
 if($uiProcess -and !$uiProcess.HasExited){$null=$uiProcess.CloseMainWindow();$null=$uiProcess.WaitForExit(5000);if(!$uiProcess.HasExited){Stop-Process -Id $uiProcess.Id -Force}}
 if($hostProcess -and !$hostProcess.HasExited){try{Request quit-host|Out-Null;$null=$hostProcess.WaitForExit(5000)}catch{};if(!$hostProcess.HasExited){Stop-Process -Id $hostProcess.Id -Force}}
}
