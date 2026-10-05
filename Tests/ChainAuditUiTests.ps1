param([string]$EntryPoint='', [switch]$KeepOpen, [switch]$ExerciseDeviceModal, [switch]$ExercisePointers, [switch]$ExercisePluginBuses, [switch]$ExerciseChannelOptions, [switch]$ExerciseDialogs, [switch]$PhysicalDialogClicks)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/HostProtocol.ps1"
$repo=[IO.Path]::GetFullPath("$PSScriptRoot/..")
if(!$EntryPoint){$EntryPoint=Join-Path $repo 'out/build/windows-vs2022/LightHostModern_artefacts/Release/LightHostModern.exe'}
$testRoot=Join-Path $repo 'out/test-profiles';$name='chain-audit-ui-'+[guid]::NewGuid().ToString('N')
$profile=Join-Path $testRoot $name;$output=Join-Path $profile 'captures'
$script:hostProcess=$null;$script:uiProcess=$null;$script:hostSession=''
function Request($command,$arguments=@()) {
 $r=Send-HostRequest $script:info.pipe $command $arguments -Session $script:hostSession
 if($r.hostSession){$script:hostSession=$r.hostSession};return $r
}
function Resolve($r) {
 $deadline=[DateTime]::UtcNow.AddSeconds(15)
 while($r.status -eq 'operation'){
  if($r.operationState -in @('completed','failed','cancelled')){return $r.result}
  if([DateTime]::UtcNow -gt $deadline){throw 'IPC operation timed out'}
  Start-Sleep -Milliseconds 40;$r=Request operation-status @($r.operationId,$r.hostSession)
 };return $r
}
function Change($value) {
 $s=Request snapshot;$value.revision=[string]$s.chainVersion;$value.profileId=$s.operating.activeProfile;$value.generation=[string]$s.operating.generation
 $r=Resolve (Request operating-command @($value));if($r.status -ne 'ok'){throw ($r|ConvertTo-Json -Depth 12)}
}
function Start-Isolated {
 $script:hostSession='';$oldPid=if($script:hostProcess){$script:hostProcess.Id}else{0}
 $launcher=Start-Process $EntryPoint -ArgumentList @("--test-profile=$name","--profile-root=`"$testRoot`"") -WindowStyle Hidden -PassThru
 $deadline=[DateTime]::UtcNow.AddSeconds(40)
 do {
  try {
   $script:info=Get-Content (Join-Path $profile 'profile.json') -Raw|ConvertFrom-Json
   if($script:info.pid -ne $oldPid){
    $processInfo=Get-CimInstance Win32_Process -Filter "ProcessId=$($script:info.pid)"
    if($processInfo.CommandLine.Contains("--test-profile=$name")){
     $s=Request snapshot;if($s.hostPid -eq $script:info.pid){$script:hostProcess=Get-Process -Id $script:info.pid;return}
    }
   }
  }catch{}
  Start-Sleep -Milliseconds 100
 }while([DateTime]::UtcNow -lt $deadline)
 throw 'Isolated host startup timed out'
}
function Quit-Isolated {Request quit-host|Out-Null;if(!$script:hostProcess.WaitForExit(15000)){throw 'Host shutdown timed out'}}
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class AuditUiWindow {
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern void keybd_event(byte key,byte scan,uint flags,UIntPtr extra);
}
'@
function Find([string]$key) {
 $e=$script:window.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$key))
 if(!$e){$e=$script:window.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$key))}
 if(!$e){throw "UI control not found: $key"};return $e
}
function Invoke([string]$key) {
 if(!$ExerciseDialogs -and [Windows.Automation.AutomationElement]::FocusedElement.Current.ProcessId -ne $script:uiProcess.Id){Focus-Window}
 $e=Find $key;$pattern=$null
 if($e.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)){$pattern.Invoke()}
 else{$e.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()}
 Start-Sleep -Milliseconds 500
}
function Capture([string]$label) {
 & "$repo/Utilities/Capture UI Review.ps1" -UiPid $script:uiProcess.Id -Name $label -OutputDirectory $output -PreserveInteraction:([bool]$script:preserveCaptureInteraction)
}
function Focus-Window {
 if([AuditUiWindow]::GetForegroundWindow() -eq $script:uiProcess.MainWindowHandle){return}
 [AuditUiWindow]::ShowWindow($script:uiProcess.MainWindowHandle,9)|Out-Null
 [AuditUiWindow]::keybd_event(18,0,0,[UIntPtr]::Zero)
 [AuditUiWindow]::keybd_event(18,0,2,[UIntPtr]::Zero)
 $activation=New-Object -ComObject WScript.Shell
 if(!$activation.AppActivate($script:uiProcess.Id)){throw 'Cannot activate isolated keyboard test window'}
 [AuditUiWindow]::SetForegroundWindow($script:uiProcess.MainWindowHandle)|Out-Null
 $deadline=[DateTime]::UtcNow.AddSeconds(3)
 while([AuditUiWindow]::GetForegroundWindow() -ne $script:uiProcess.MainWindowHandle){
  if([DateTime]::UtcNow -gt $deadline){$focused=[Windows.Automation.AutomationElement]::FocusedElement;throw "Desktop focus is unavailable for the isolated keyboard test (expected $($script:uiProcess.Id), actual $($focused.Current.ProcessId): $($focused.Current.Name))"}
  Start-Sleep -Milliseconds 50
 }
}
function Send-Key($Element,[string]$Keys) {
 Focus-Window
 $Element.SetFocus();Start-Sleep -Milliseconds 100
 if([Windows.Automation.AutomationElement]::FocusedElement.Current.ProcessId -ne $script:uiProcess.Id){throw 'Desktop focus changed before sending keys; no keys sent'}
 [Windows.Forms.SendKeys]::SendWait($Keys)
}
function Check-KeyboardMove([string]$NodeId) {
 $attempts=[Collections.Generic.List[object]]::new()
 $controlId='ChainMenu-'+$NodeId
 try {
  for($attempt=1;$attempt -le 3;$attempt++){
   $before=Request snapshot
   $beforeNode=@($before.operating.graph.nodes|Where-Object id -eq $NodeId)[0]
   if(!$beforeNode){throw 'Keyboard movement target disappeared'}
   Send-Key (Find $controlId) '{RIGHT}'
   # Do not reactivate/refocus while assessing retention. Re-activation can focus
   # the root window and cannot prove whether saving replaced the original card.
   $samples=[Collections.Generic.List[object]]::new();$externalFocus=$false
   $until=[DateTime]::UtcNow.AddMilliseconds(900)
   do {
    $foreground=[AuditUiWindow]::GetForegroundWindow()
    $focus=[Windows.Automation.AutomationElement]::FocusedElement
    $focusPid=if($focus){$focus.Current.ProcessId}else{0}
    if($foreground -ne $script:uiProcess.MainWindowHandle -or $focusPid -ne $script:uiProcess.Id){$externalFocus=$true}
    $samples.Add([pscustomobject]@{foreground=$foreground.ToInt64();process=$focusPid;control=if($focusPid -eq $script:uiProcess.Id){$focus.Current.AutomationId}else{''}})
    Start-Sleep -Milliseconds 25
   }while([DateTime]::UtcNow -lt $until)
   $after=Request snapshot;$saved=@($after.operating.graph.nodes|Where-Object id -eq $NodeId)[0]
   $focus=[Windows.Automation.AutomationElement]::FocusedElement
   if([AuditUiWindow]::GetForegroundWindow() -ne $script:uiProcess.MainWindowHandle -or !$focus -or $focus.Current.ProcessId -ne $script:uiProcess.Id){$externalFocus=$true}
   $record=[pscustomobject]@{attempt=$attempt;beforeX=$beforeNode.x;afterX=$saved.x;externalFocus=$externalFocus;samples=@($samples);status='checking'}
   $attempts.Add($record)
   if($externalFocus){
    $record.status='interrupted-by-desktop-focus'
    Write-Host "Keyboard focus trial $attempt was interrupted by another window; no focus-retention result is claimed for that trial."
    if($attempt -eq 3){throw 'Desktop focus repeatedly interrupted the isolated keyboard test; rerun with the desktop available.'}
    continue
   }
   if($focus.Current.AutomationId -ne $controlId){
    $record.status='failed-focus-retention'
    throw "Saving a keyboard move lost card focus while the test window stayed active: $($focus.Current.AutomationId) / $($focus.Current.Name)"
   }
   if(!$saved -or $saved.x -le $beforeNode.x){$record.status='failed-persistence';throw 'Keyboard movement was not persisted'}
   $record.status='passed';return $after
  }
 }finally{$attempts|ConvertTo-Json -Depth 8|Set-Content (Join-Path $profile 'keyboard-focus-trials.json') -Encoding UTF8}
}
try {
 Start-Isolated;Change @{action='mode';mode='chain';discard=$true};Quit-Isolated;Start-Isolated
 $started=Request snapshot
 if($started.operating.mode -ne 'chain' -or $started.operating.pendingMode){throw 'Restart did not consume the pending operating mode'}
 Change @{action='add';kind='mixer';x=280;y=100};Change @{action='add';kind='mixer';x=820;y=130}
 $s=Request snapshot;$mixer=@($s.operating.graph.nodes|Where-Object kind -eq 'mixer')[0]
 $mixer.customName='Recovered mixer';$mixer.x=340
 $recovery=@{profileId=$s.operating.activeProfile;generation=$s.operating.generation;graph=$s.operating.graph}
 [IO.File]::WriteAllText((Join-Path $profile ("chain-edit-recovery-"+$s.operating.activeProfile+".json")),($recovery|ConvertTo-Json -Depth 64),(New-Object Text.UTF8Encoding($false)))
 [IO.File]::WriteAllText((Join-Path $profile 'ui-settings.ini'),"[Appearance]`r`nThemeMode=Dark`r`nLayoutMode=Expanded`r`nBackdropMode=1`r`n[Localization]`r`nLanguage=en-us`r`n",[Text.Encoding]::Unicode)
 # Use the UI delivered beside the selected host, including versioned portables.
 $hostPath=(Get-CimInstance Win32_Process -Filter "ProcessId=$($script:hostProcess.Id)").ExecutablePath
 $uiExe=Join-Path (Split-Path $hostPath) 'WinUI/x64/Release/LightHostModern.WinUI/LightHostModernWinUI.exe'
 $script:uiProcess=Start-Process $uiExe -ArgumentList @("--test-profile=$name","--profile-root=`"$testRoot`"","--host-pipe=`"$($script:info.pipe)`"") -WindowStyle Hidden -PassThru
 $deadline=[DateTime]::UtcNow.AddSeconds(30)
 do {Start-Sleep -Milliseconds 200;$script:uiProcess.Refresh()}while((!$script:uiProcess.MainWindowHandle -or !$script:uiProcess.MainWindowTitle.Contains('[Test:')) -and [DateTime]::UtcNow -lt $deadline)
 if(!$script:uiProcess.MainWindowTitle.Contains('[Test:')){throw 'Isolated UI did not start'}
 [AuditUiWindow]::ShowWindow($script:uiProcess.MainWindowHandle,9)|Out-Null
 $script:window=[Windows.Automation.AutomationElement]::FromHandle($script:uiProcess.MainWindowHandle)
 & "$repo/Utilities/Capture UI Review.ps1" -UiPid $script:uiProcess.Id -PrepareOnly -OutputDirectory $output
 Invoke NavPlugins;Capture 'chain-recovery-pending'
 Invoke ReviewChainRecovery;Invoke 'Restore edit';Start-Sleep -Milliseconds 500
 $after=Request snapshot;$saved=@($after.operating.graph.nodes|Where-Object id -eq $mixer.id)[0]
 if($saved.customName -ne 'Recovered mixer' -or $saved.x -ne 340){throw 'Recovered graph was not restored'}
 if(!$ExercisePluginBuses -and !$ExerciseChannelOptions -and (!$ExerciseDialogs -or $ExercisePointers)){
 $after=Check-KeyboardMove $mixer.id
 Capture 'chain-keyboard-movement'
 # Open a port's destination menu and connect another mixer using only keys.
 $second=@($after.operating.graph.nodes|Where-Object { $_.kind -eq 'mixer' -and $_.id -ne $mixer.id })[0]
 $outputPort=Find ('ChainPort-'+$mixer.id+'-out-0')
 if(!$outputPort){throw 'Accessible output port missing'}
 Send-Key $outputPort '{ENTER}';Start-Sleep -Milliseconds 400
 $choices=$script:window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::MenuItem))
 $destination=@($choices|Where-Object {$_.Current.Name.StartsWith('Mixer ') -or $_.Current.Name.StartsWith('Mixer?')})[0]
 if(!$destination){throw 'Keyboard connection menu missing a mixer destination'}
 Send-Key $destination '{ENTER}';Start-Sleep -Milliseconds 900
 $connected=Request snapshot
 if(!@($connected.operating.graph.edges|Where-Object {$_.from -eq $mixer.id -and $_.to -eq $second.id}).Count){throw 'Keyboard connection was not persisted'}
 Capture 'chain-keyboard-connection'
 }
 if($ExercisePointers){. "$PSScriptRoot/ChainPointerScenarios.ps1"}
 elseif($ExercisePluginBuses){. "$PSScriptRoot/PluginBusUiScenarios.ps1"}
 if($ExerciseChannelOptions){. "$PSScriptRoot/ChannelOptionsUiScenarios.ps1"}
 if($ExerciseDialogs){. "$PSScriptRoot/DialogInteractionUiScenarios.ps1"}
 foreach($page in 'Profiles','Settings','Audio','Diagnostics','Dashboard'){Invoke ('Nav'+$page);Capture $page.ToLowerInvariant()}
 $deviceModalMemory=@()
 if($ExerciseDeviceModal){
  Invoke NavSettings
  for($cycle=1;$cycle -le 20;$cycle++){
   Invoke ManageEnabledAudioDevices;Invoke Cancel
   if($cycle % 4 -eq 0){
    $script:uiProcess.Refresh()
    $deviceModalMemory+=@{cycle=$cycle;privateBytes=$script:uiProcess.PrivateMemorySize64;workingSetBytes=$script:uiProcess.WorkingSet64}
   }
  }
  $deviceModalMemory|ConvertTo-Json|Set-Content (Join-Path $profile 'device-modal-memory.json') -Encoding UTF8
  # The reproduced cycle retained ~5 MiB per opening. Allow framework caching,
  # but fail sustained growth above 8 MiB across 16 warm openings. This is a
  # bounded regression, not proof of zero long-term framework retention.
  $growth=$deviceModalMemory[-1].privateBytes-$deviceModalMemory[0].privateBytes
  if($growth -gt 8*1024*1024){throw "Device dialogs retained $growth bytes across 16 warm openings"}
  Capture 'settings-after-device-dialog-cycles'
 }
 Invoke NavPlugins
 $result=@{passed=$true;hostPath=$hostPath;hostPid=$script:hostProcess.Id;uiPid=$script:uiProcess.Id;profile=$profile;recovery=$true;keyboardMovement=(!$ExercisePluginBuses -and !$ExerciseChannelOptions -and (!$ExerciseDialogs -or $ExercisePointers));keyboardConnection=(!$ExercisePluginBuses -and !$ExerciseChannelOptions -and (!$ExerciseDialogs -or $ExercisePointers));channelOptions=[bool]$ExerciseChannelOptions;pluginBuses=[bool]($ExercisePluginBuses -or $ExercisePointers);dialogs=[bool]$ExerciseDialogs;deviceModalMemory=$deviceModalMemory}
 $result|ConvertTo-Json|Set-Content (Join-Path $profile 'ui-result.json') -Encoding UTF8;$result|ConvertTo-Json
} catch {
 if($script:uiProcess -and !$script:uiProcess.HasExited){try{Capture 'failure'}catch{}}
 throw
} finally {
 if(!$KeepOpen){
  if($script:uiProcess -and !$script:uiProcess.HasExited){$null=$script:uiProcess.CloseMainWindow();$null=$script:uiProcess.WaitForExit(6000);if(!$script:uiProcess.HasExited){Stop-Process -Id $script:uiProcess.Id -Force}}
  if($script:hostProcess -and !$script:hostProcess.HasExited){try{Quit-Isolated}catch{Stop-Process -Id $script:hostProcess.Id -Force}}
 }
}
