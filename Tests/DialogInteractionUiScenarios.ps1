# Run inside ChainAuditUiTests. Each action is activated exactly once.
# Use -PhysicalDialogClicks for pointer capture/focus regression coverage when
# the desktop is available; the default uses UIA without moving the user's mouse.
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class DialogMouse {
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern void mouse_event(uint flags,uint x,uint y,uint data,UIntPtr extra);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
 public struct Point {public int X,Y;}
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out Point point);
}
'@
[DialogMouse]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
function Visible-DialogButton([string]$label){
 @($script:window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.AndCondition]::new(
  [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::Button),
  [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$label)))|Where-Object {!$_.Current.IsOffscreen})|Select-Object -First 1
}
function Wait-DialogButton([string]$label,[bool]$visible=$true){
 $until=[DateTime]::UtcNow.AddSeconds(8)
 do{$found=Visible-DialogButton $label;if([bool]$found -eq $visible){if(!$visible){Start-Sleep -Milliseconds 350};return $found};Start-Sleep -Milliseconds 50}while([DateTime]::UtcNow -lt $until)
 throw "Dialog action '$label' did not become visible=$visible after one activation"
}
function Click-DialogOnce([string]$label){
 $button=Wait-DialogButton $label
 # Let the native opening transition settle before measuring its target.
 Start-Sleep -Milliseconds 350
 $r=$button.Current.BoundingRectangle
 if(!$button.Current.IsEnabled -or $r.IsEmpty){throw "Dialog action is not clickable: $label"}
 Write-Output "Activate once: $label"
 if($PhysicalDialogClicks){
  Focus-Window
  $x=[int]($r.X+$r.Width/2);$y=[int]($r.Y+$r.Height/2)
  [DialogMouse]::SetCursorPos($x,$y)|Out-Null
  Start-Sleep -Milliseconds 60
  $cursor=New-Object DialogMouse+Point;[DialogMouse]::GetCursorPos([ref]$cursor)|Out-Null
  if([Math]::Abs($cursor.X-$x) -gt 3 -or [Math]::Abs($cursor.Y-$y) -gt 3 -or [AuditUiWindow]::GetForegroundWindow() -ne $script:uiProcess.MainWindowHandle){throw 'Desktop input changed before the dialog click; no click sent'}
  [DialogMouse]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
  try{Start-Sleep -Milliseconds 60}finally{[DialogMouse]::mouse_event(4,0,0,0,[UIntPtr]::Zero)}
 }else{$button.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()}
}
function Assert-DialogOrder([string[]]$labels){
 $previous=-1.0
 foreach($label in $labels){$button=Wait-DialogButton $label;$r=$button.Current.BoundingRectangle;if($r.X -le $previous){throw "Incorrect modal action order: $labels"};$previous=$r.X}
}
function Request-ListMode {
 Invoke NavSettings
 $box=Find SettingsOperatingMode
 $box.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
 Start-Sleep -Milliseconds 200
 $choice=$box.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'List'))
 if(!$choice){$choice=Find List}
 $choice.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
 Wait-DialogButton Discard|Out-Null
}
function Capture-Modal([string]$label){& "$repo/Utilities/Capture UI Review.ps1" -UiPid $script:uiProcess.Id -Name $label -OutputDirectory $output -Passive}

# Dirty default Chain profile: cancel, save through the name dialog, then discard.
Change @{action='add';kind='mixer';x=800;y=700}
Start-Sleep -Milliseconds 500
Request-ListMode
Assert-DialogOrder @('Cancel','Discard','Save')
Capture-Modal 'dialog-save-discard-order'
Click-DialogOnce Cancel
Wait-DialogButton Discard $false|Out-Null
if((Request snapshot).operating.pendingMode){throw 'Cancel still changed the pending mode'}
Request-ListMode
Click-DialogOnce Save
Wait-DialogButton Discard $false|Out-Null
$until=[DateTime]::UtcNow.AddSeconds(8);$nameBox=$null
do{try{$nameBox=Find ProfileName}catch{};if(!$nameBox){Start-Sleep -Milliseconds 50}}while(!$nameBox -and [DateTime]::UtcNow -lt $until)
if(!$nameBox){throw 'Save did not open the profile name dialog on the first click'}
Assert-DialogOrder @('Cancel','Save')
$nameBox.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('Single click regression')
Click-DialogOnce Save
Wait-DialogButton 'Restart later'|Out-Null
Assert-DialogOrder @('Restart later','Restart now')
Capture-Modal 'dialog-restart-order'
Click-DialogOnce 'Restart later'
Wait-DialogButton 'Restart later' $false|Out-Null
if((Request snapshot).operating.pendingMode -ne 'list'){throw 'Save/restart later lost the selected mode'}

# Revert the pending mode, edit again, and verify Discard on the first click.
Change @{action='mode';mode='chain';discard=$true}
Change @{action='add';kind='mixer';x=1000;y=900}
Start-Sleep -Milliseconds 700
Request-ListMode
Click-DialogOnce Discard
Wait-DialogButton 'Restart later'|Out-Null
Click-DialogOnce 'Restart later'
Wait-DialogButton 'Restart later' $false|Out-Null

# Diagnostics has a second restart dialog. Verify Later, then actual Restart
# without touching the user's normal host/profile or audio devices.
Invoke NavDiagnostics
(Find TrackVerboseLogs).GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
Wait-DialogButton 'Restart later'|Out-Null
Assert-DialogOrder @('Restart later','Restart now')
Click-DialogOnce 'Restart later'
Wait-DialogButton 'Restart later' $false|Out-Null
$until=[DateTime]::UtcNow.AddSeconds(8)
do{$restartLink=Find VerboseLogsRestartLink;if($restartLink.Current.IsEnabled){break};Start-Sleep -Milliseconds 50}while([DateTime]::UtcNow -lt $until)
if(!$restartLink.Current.IsEnabled){throw 'Verbose restart link stayed disabled after the dialog closed'}
$restartLink.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
$oldHost=$script:hostProcess.Id;$oldUi=$script:uiProcess.Id
Click-DialogOnce 'Restart now'
$deadline=[DateTime]::UtcNow.AddSeconds(35)
do{
 Start-Sleep -Milliseconds 150
 $candidate=Get-Content (Join-Path $profile 'profile.json') -Raw|ConvertFrom-Json
 if($candidate.pid -ne $oldHost -and (Get-Process -Id $candidate.pid -ErrorAction SilentlyContinue)){
  $script:info=$candidate;$script:hostSession='';$script:hostProcess=Get-Process -Id $candidate.pid
  $found=Get-CimInstance Win32_Process -Filter "Name='LightHostModernWinUI.exe'"|Where-Object {$_.ProcessId -ne $oldUi -and $_.CommandLine -and $_.CommandLine.Contains("--test-profile=$name")}|Select-Object -First 1
  if($found){$script:uiProcess=Get-Process -Id $found.ProcessId;if($script:uiProcess.MainWindowTitle.Contains('[Test:')){break}}
 }
}while([DateTime]::UtcNow -lt $deadline)
if($script:hostProcess.Id -eq $oldHost -or $script:uiProcess.Id -eq $oldUi -or !$script:uiProcess.MainWindowTitle.Contains('[Test:')){throw 'One Restart now click did not restart the isolated host and UI'}
$script:window=[Windows.Automation.AutomationElement]::FromHandle($script:uiProcess.MainWindowHandle)
& "$repo/Utilities/Capture UI Review.ps1" -UiPid $script:uiProcess.Id -PrepareOnly -OutputDirectory $output
if((Request snapshot).operating.mode -ne 'list'){throw 'Restart did not apply List mode'}
if((Request verbose-log-status).phase -ne 'collecting'){throw 'Restart did not arm verbose collection'}
Resolve (Request stop-verbose-logs)|Out-Null
@{passed=$true;physicalClicks=[bool]$PhysicalDialogClicks;cancelSingleActivation=$true;saveSingleActivation=$true;discardSingleActivation=$true;restartLaterSingleActivation=$true;restartNowSingleActivation=$true;primaryRight=$true}|
 ConvertTo-Json|Set-Content (Join-Path $profile 'dialog-result.json') -Encoding UTF8
