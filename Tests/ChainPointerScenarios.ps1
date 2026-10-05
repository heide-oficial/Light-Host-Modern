# Run inside ChainAuditUiTests' isolated profile and real UI.
$script:preserveCaptureInteraction=$true
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ChainMouse {
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern void mouse_event(uint flags,uint dx,uint dy,uint data,UIntPtr extra);
 [DllImport("user32.dll")]public static extern void keybd_event(byte key,byte scan,uint flags,UIntPtr extra);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 public struct Point { public int X,Y; }
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out Point point);
}
'@
[ChainMouse]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
function Mouse-Point([double]$x,[double]$y){
 [ChainMouse]::SetCursorPos([int]$x,[int]$y)|Out-Null
 # SetCursorPos alone may not deliver WinUI hover updates; send real motion.
 [ChainMouse]::mouse_event(1,1,0,0,[UIntPtr]::Zero)
 Start-Sleep -Milliseconds 60
}
function Mouse-Drag($from,$to,[int]$holdMs=0,[string]$previewCapture=''){
 Focus-Window
 Mouse-Point $from[0] $from[1]
 [ChainMouse]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
 try{
  Start-Sleep -Milliseconds 100
  for($step=1;$step -le 14;$step++){
   Mouse-Point ($from[0]+($to[0]-$from[0])*$step/14) ($from[1]+($to[1]-$from[1])*$step/14)
  }
  if($holdMs){Start-Sleep -Milliseconds $holdMs}
  if($previewCapture){Capture $previewCapture}
  $cursor=New-Object ChainMouse+Point;[ChainMouse]::GetCursorPos([ref]$cursor)|Out-Null
  if([Math]::Abs($cursor.X-$to[0]) -gt 6 -or [Math]::Abs($cursor.Y-$to[1]) -gt 6){throw "Pointer moved during test: expected $to; actual $($cursor.X),$($cursor.Y)"}
 }finally{[ChainMouse]::mouse_event(4,0,0,0,[UIntPtr]::Zero)}
 Start-Sleep -Milliseconds 700
}
function Select-Channel([string]$key,[int]$index){
 $keys='{F4}{HOME}'+('{DOWN}'*$index)+'{ENTER}'
 Send-Key (Find $key) $keys;Start-Sleep -Milliseconds 200
}
function Visible-MenuNames {
 @($script:window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::MenuItem))|Where-Object {!$_.Current.IsOffscreen}|ForEach-Object {$_.Current.Name})
}
function Center($element){$r=$element.Current.BoundingRectangle;return @(($r.X+$r.Width/2),($r.Y+$r.Height/2))}
function Wait-Edges([int]$count){
 $until=[DateTime]::UtcNow.AddSeconds(5)
 do{$snap=Request snapshot;if(@($snap.operating.graph.edges).Count -eq $count){return $snap};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $until)
 throw "Expected $count pointer-created connections; received $(@($snap.operating.graph.edges).Count)"
}
Change @{action='add';kind='mixer';x=520;y=550}
$s=Request snapshot;$mixers=@($s.operating.graph.nodes|Where-Object kind -eq 'mixer')
$a=$mixers[0];$b=$mixers[1];$c=$mixers[2]
$a.x=0;$a.y=0;$b.x=1060;$b.y=0;$c.x=520;$c.y=550
$s.operating.graph.edges=@()
foreach($hardware in @($s.operating.graph.nodes|Where-Object {$_.kind -in @('input','output')})){$hardware.y=-230}
Change @{action='graph';graph=$s.operating.graph}
Focus-Window;Start-Sleep -Milliseconds 700
Invoke ChainFit;Start-Sleep -Milliseconds 500
$out=Find ('ChainPort-'+$a.id+'-out-0');$in=Find ('ChainPort-'+$b.id+'-in-0')
Mouse-Drag (Center $out) (Center $in) -holdMs 300 -previewCapture 'wire-drag-source-and-target'
$connected=Wait-Edges 1
if($connected.operating.graph.edges[0].from -ne $a.id -or $connected.operating.graph.edges[0].to -ne $b.id){throw 'Pointer connection used the wrong ports'}
Capture 'wire-pointer-connected'
# Hover/right-click use the same visible wire; a midpoint of the cubic is
# exactly the midpoint of the two sockets for the current symmetric handles.
$outRect=(Find ('ChainPort-'+$a.id+'-out-0')).Current.BoundingRectangle
$inRect=(Find ('ChainPort-'+$b.id+'-in-0')).Current.BoundingRectangle
$scale=$outRect.Height/32
$mid=@((($outRect.Right-10*$scale+$inRect.X+10*$scale)/2),(($outRect.Y+$outRect.Height/2+$inRect.Y+$inRect.Height/2)/2))
Mouse-Point $mid[0] $mid[1];Start-Sleep -Milliseconds 300;Capture 'wire-hover'
$hint=Find 'ChainWireHint'
if($hint.Current.IsOffscreen){throw 'Wire hover feedback is not visible'}
[ChainMouse]::mouse_event(8,0,0,0,[UIntPtr]::Zero);[ChainMouse]::mouse_event(16,0,0,0,[UIntPtr]::Zero)
Start-Sleep -Milliseconds 300
$wireItems=@(Visible-MenuNames)
if($wireItems.Count -ne 1 -or $wireItems[0] -ne 'Disconnect'){throw "Wire menu contains unrelated actions: $wireItems"}
Capture 'wire-context-menu'
Invoke Disconnect
Wait-Edges 0|Out-Null
# Dragging backwards is also supported.
Mouse-Drag (Center (Find ('ChainPort-'+$b.id+'-in-0'))) (Center (Find ('ChainPort-'+$a.id+'-out-0')))
Wait-Edges 1|Out-Null;Start-Sleep -Milliseconds 500
# Drop a mixer with the wire crossing the top of its card, not its center.
# Its first input/output must replace the old edge in one undoable edit.
$menu=Find ('ChainMenu-'+$c.id);$rect=$menu.Current.BoundingRectangle
$start=@(($rect.X-100*$scale),($rect.Y+$rect.Height/2))
$cardLeft=$rect.Right-346*$scale
$drop=@(($start[0]+($mid[0]-180*$scale-$cardLeft)),($mid[1]-20*$scale))
Mouse-Drag $start $drop
Select-Channel ChainInsertInput0 1
Capture 'wire-insertion-confirmation'
Invoke Insert
$inserted=Wait-Edges 2
if(!@($inserted.operating.graph.edges|Where-Object {$_.from -eq $a.id -and $_.to -eq $c.id}).Count -or
   !@($inserted.operating.graph.edges|Where-Object {$_.from -eq $c.id -and $_.to -eq $b.id}).Count){throw 'Insertion did not preserve both ends'}
if(@($inserted.operating.graph.edges|Where-Object {$_.to -eq $c.id})[0].input -ne 2){throw 'Insertion ignored the selected mixer input pair'}
Capture 'wire-inserted'
Invoke Undo
$undone=Wait-Edges 1
if($undone.operating.graph.edges[0].from -ne $a.id -or $undone.operating.graph.edges[0].to -ne $b.id){throw 'Undo did not restore the original wire'}
# A real test VST3 must also splice its visible stereo inputs/outputs.
$fixture=Join-Path $repo 'out/build/windows-vs2022/scn/LHFixture_artefacts/Release/VST3/LightHostModern Scenario Fixture.vst3'
if(!(Test-Path -LiteralPath $fixture)){throw 'Build LHFixture_VST3 before the pointer regression'}
Resolve (Request begin-plugin-scan)|Out-Null
Resolve (Request scan-plugin-path @($fixture))|Out-Null
$until=[DateTime]::UtcNow.AddSeconds(30)
while((Request plugin-scan-status).active){if([DateTime]::UtcNow -gt $until){throw 'Fixture scan timed out'};Start-Sleep -Milliseconds 100}
$known=@((Request snapshot).knownPluginList)[0]
if(!$known.knownId){throw 'Test fixture was not discovered'}
Resolve (Request add-known-plugin @($known.knownId))|Out-Null
$until=[DateTime]::UtcNow.AddSeconds(15)
do{$s=Request snapshot;$plugin=@($s.activePlugins)[0];if($plugin.loading -eq 'loaded'){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $until)
if($plugin.loading -ne 'loaded'){throw 'Test fixture did not load'}
$pluginId=$plugin.instanceId
function Place-Fixture {
 $state=Request snapshot;$n=@($state.operating.graph.nodes|Where-Object id -eq $pluginId)[0]
 $n.x=520;$n.y=550
 $n|Add-Member -NotePropertyName splitInputs -NotePropertyValue $true -Force
 $n|Add-Member -NotePropertyName splitOutputs -NotePropertyValue $true -Force
 $other=@($state.operating.graph.nodes|Where-Object id -eq $c.id)[0];$other.x=0;$other.y=550
 Change @{action='graph';graph=$state.operating.graph}
 Start-Sleep -Milliseconds 500;Invoke ChainFit
}
function Drop-Fixture {
 $outRect=(Find ('ChainPort-'+$a.id+'-out-0')).Current.BoundingRectangle
 $inRect=(Find ('ChainPort-'+$b.id+'-in-0')).Current.BoundingRectangle
 $scale=$outRect.Height/32
 $mid=@((($outRect.Right+$inRect.X)/2),(($outRect.Y+$outRect.Height/2+$inRect.Y+$inRect.Height/2)/2))
 $r=(Find ('ChainMenu-'+$pluginId)).Current.BoundingRectangle
 $start=@(($r.X-100*$scale),($r.Y+$r.Height/2))
 $drop=@(($start[0]+($mid[0]-180*$scale-($r.Right-346*$scale))),($mid[1]-20*$scale))
 Mouse-Drag $start $drop
}
Place-Fixture;Drop-Fixture
Capture 'wire-plugin-insertion-confirmation'
Invoke 'Keep position only'
$kept=Wait-Edges 1
if(@($kept.operating.graph.edges|Where-Object {$_.from -eq $pluginId -or $_.to -eq $pluginId}).Count){throw 'Canceling insertion changed the connections'}
Place-Fixture;Drop-Fixture
Select-Channel ChainInsertInput0 1
if((Find Insert).Current.IsEnabled){throw 'Insertion allowed the same input for Left and Right'}
Select-Channel ChainInsertInput1 0
Select-Channel ChainInsertOutput0 1
Select-Channel ChainInsertOutput1 0
Capture 'wire-individual-channel-selection'
Invoke Insert
$inserted=Wait-Edges 4
if(!@($inserted.operating.graph.edges|Where-Object {$_.from -eq $a.id -and $_.to -eq $pluginId}).Count -or
   !@($inserted.operating.graph.edges|Where-Object {$_.from -eq $pluginId -and $_.to -eq $b.id}).Count){throw 'Plugin insertion did not preserve both ends'}
if(@($inserted.operating.graph.edges|Where-Object {$_.sourceWidth -ne 1 -or $_.targetWidth -ne 1}).Count){throw 'Individual channel choices were not routed separately'}
foreach($lane in 0,1){
 if(!@($inserted.operating.graph.edges|Where-Object {$_.from -eq $a.id -and $_.to -eq $pluginId -and $_.output -eq $lane -and $_.input -eq (1-$lane)}).Count -or
    !@($inserted.operating.graph.edges|Where-Object {$_.from -eq $pluginId -and $_.to -eq $b.id -and $_.output -eq (1-$lane) -and $_.input -eq $lane}).Count){throw 'Individual input/output selection was ignored'}
}
Capture 'wire-plugin-inserted'
Invoke ('ChainMenu-'+$pluginId)
$cardItems=Visible-MenuNames
$expected=@('Open editor','Bypass','Retry loading','Duplicate','Run in a separate process (experimental)','Rename','Card color','Arrange layers','Horizontal channels','Configure plugin channels','Input channels','Output channels','Remove')
if(($cardItems -join '|') -ne ($expected -join '|')){throw "Unexpected plugin card menu: $cardItems"}
Capture 'plugin-card-context-menu'
Send-Key (Find 'Open editor') '{ESC}'
Send-Key (Find 'Plugin chain canvas') '{ESC}'
$rect=(Find 'Plugin chain canvas').Current.BoundingRectangle
Mouse-Point ($rect.X+45) ($rect.Y+$rect.Height/2)
[ChainMouse]::mouse_event(8,0,0,0,[UIntPtr]::Zero);[ChainMouse]::mouse_event(16,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 300
$backgroundItems=Visible-MenuNames
if(($backgroundItems -join '|') -ne 'Add plugin|Add mixer|Organize|Mute output|Bypass chain'){throw "Unexpected canvas menu: $backgroundItems"}
Capture 'canvas-context-menu'
Send-Key (Find 'Add plugin') '{ESC}'
Invoke Undo
Wait-Edges 1|Out-Null
# Select two mixers and verify the matching channel submenus for bulk actions.
Send-Key (Find 'Plugin chain canvas') '{ESC}'
function Select-Card([string]$id,[bool]$additive){
 $r=(Find ('ChainMenu-'+$id)).Current.BoundingRectangle
 $scale=$r.Height/28
 Mouse-Point ($r.X-90*$scale) ($r.Y+$r.Height/2)
 if($additive){[ChainMouse]::keybd_event(17,0,0,[UIntPtr]::Zero)}
 try{[ChainMouse]::mouse_event(2,0,0,0,[UIntPtr]::Zero);[ChainMouse]::mouse_event(4,0,0,0,[UIntPtr]::Zero)}
 finally{if($additive){[ChainMouse]::keybd_event(17,0,2,[UIntPtr]::Zero)}}
 Start-Sleep -Milliseconds 150
}
Select-Card $a.id $false;Select-Card $b.id $true
[ChainMouse]::mouse_event(8,0,0,0,[UIntPtr]::Zero);[ChainMouse]::mouse_event(16,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 300
$batchItems=Visible-MenuNames
if(($batchItems -join '|') -ne 'Card color|Arrange layers|Horizontal channels|Input channels|Output channels|Delete selected elements'){throw "Unexpected bulk card menu: $batchItems"}
Capture 'multiple-card-context-menu'
Send-Key (Find 'Input channels') '{RIGHT}'
if(!(Find 'Disconnect input wires').Current.IsEnabled){throw 'Bulk input disconnection was disabled despite incoming wires'}
Capture 'multiple-input-context-menu'
Send-Key (Find 'Disconnect input wires') '{ESC}{ESC}'
# The selection is still active: clicking a selected channel must keep the bulk menu.
$portCenter=Center (Find ('ChainPort-'+$a.id+'-out-0'));Mouse-Point $portCenter[0] $portCenter[1]
[ChainMouse]::mouse_event(8,0,0,0,[UIntPtr]::Zero);[ChainMouse]::mouse_event(16,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 300
if((Visible-MenuNames)[-1] -ne 'Delete selected elements'){throw 'Selected channel did not retain the bulk menu with Delete last'}
Send-Key (Find 'Card color') '{ESC}'
# Right-click empty space without first clearing the selection.
$rect=(Find 'Plugin chain canvas').Current.BoundingRectangle;Mouse-Point ($rect.X+45) ($rect.Y+$rect.Height/2)
[ChainMouse]::mouse_event(8,0,0,0,[UIntPtr]::Zero);[ChainMouse]::mouse_event(16,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 300
if(((Visible-MenuNames) -join '|') -ne 'Add plugin|Add mixer|Organize|Mute output|Bypass chain'){throw 'Empty canvas retained bulk selection actions'}
Capture 'empty-canvas-clears-selection'
Send-Key (Find 'Add plugin') '{ESC}'
# Right-clicking a former selection now gives a single-card menu.
$r=(Find ('ChainMenu-'+$a.id)).Current.BoundingRectangle;Mouse-Point ($r.X-45) ($r.Y+$r.Height/2)
[ChainMouse]::mouse_event(8,0,0,0,[UIntPtr]::Zero);[ChainMouse]::mouse_event(16,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 300
if('Delete selected elements' -in (Visible-MenuNames) -or 'Remove' -notin (Visible-MenuNames)){throw 'Empty canvas did not clear the selection'}
Send-Key (Find 'Remove') '{ESC}'
Invoke 'Add plugin'
(Find ('installed-'+$known.knownId)).GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).AddToSelection()
Start-Sleep -Milliseconds 200
if(!(Find 'Add (1)').Current.IsEnabled){throw 'Selecting a plugin did not enable Add'}
Capture 'add-plugin-accent-action'
Invoke $known.knownId
$installedItems=Visible-MenuNames
if(($installedItems -join '|') -ne 'Plugin details|Open folder|Rename plugin|Remove from database'){throw "Unexpected installed plugin menu: $installedItems"}
Capture 'installed-plugin-context-menu'
Send-Key (Find 'Plugin details') '{ESC}'
Invoke Cancel
. "$PSScriptRoot/PluginBusUiScenarios.ps1" -InstanceId $pluginId
@{passed=$true;pluginBusDialog=$true;coupledMono=$true;unavailableWires=$true;stereoRestored=$true;forwardDrag=$true;reverseDrag=$true;wireHover=$true;wireContextDisconnect=$true;mixerInsertion=$true;pluginInsertion=$true;insertionCancel=$true;stereoIndividualView=$true;insertionUndo=$true;channelSelection=$true;wireOnlyMenu=$true;cardMenu=$true;bulkMenu=$true;canvasMenu=$true;installedMenu=$true}|
 ConvertTo-Json|Set-Content (Join-Path $profile 'pointer-result.json') -Encoding UTF8
$script:preserveCaptureInteraction=$false
