# Runs inside ChainAuditUiTests' isolated host and UI.
$fixture=Join-Path $repo 'out/build/windows-vs2022/scn/LHFixture_artefacts/Release/VST3/LightHostModern Scenario Fixture.vst3'
$copyRoot=Join-Path $profile 'second-fixture'
New-Item -ItemType Directory -Path $copyRoot -Force|Out-Null
Copy-Item -LiteralPath $fixture -Destination $copyRoot -Recurse
Resolve (Request begin-plugin-scan)|Out-Null
Resolve (Request scan-plugin-path @($fixture))|Out-Null
Resolve (Request scan-plugin-path @((Join-Path $copyRoot 'LightHostModern Scenario Fixture.vst3')))|Out-Null
$until=[DateTime]::UtcNow.AddSeconds(30)
while((Request plugin-scan-status).active){if([DateTime]::UtcNow -gt $until){throw 'Second fixture scan timed out'};Start-Sleep -Milliseconds 100}
$before=Request snapshot;$catalog=@($before.knownPluginList)
if($catalog.Count -lt 2){throw 'Multi-select regression requires two catalog entries'}
Start-Sleep -Milliseconds 3000
Invoke 'Add plugin'
$list=Find InstalledPluginsList
if(!$list.GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.CanSelectMultiple){throw 'Plugin picker is not multiple-selection'}
foreach($plugin in $catalog){(Find ('installed-'+$plugin.knownId)).GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).AddToSelection()}
Start-Sleep -Milliseconds 200
Find ('Add ('+$catalog.Count+')')|Out-Null
$search=Find InstalledPluginSearch
$search=$search.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::Edit))
$search.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('no matching plugin 928401')
Start-Sleep -Milliseconds 300
Find ('Add ('+$catalog.Count+')')|Out-Null
$search.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('')
Start-Sleep -Milliseconds 300
Invoke InstalledPluginSort
(Find 'Group by manufacturer').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
Start-Sleep -Milliseconds 400
if(@($list.GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()).Count -ne $catalog.Count){throw 'Sorting or filtering lost selected plugins'}
& "$repo/Utilities/Capture UI Review.ps1" -UiPid $script:uiProcess.Id -Name 'multi-plugin-selection' -OutputDirectory $output -Passive
Invoke ('Add ('+$catalog.Count+')')
$until=[DateTime]::UtcNow.AddSeconds(15)
do{$after=Request snapshot;if(@($after.activePlugins).Count -eq @($before.activePlugins).Count+$catalog.Count){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $until)
if(@($after.activePlugins).Count -ne @($before.activePlugins).Count+$catalog.Count){throw 'Add did not add every selected plugin'}
$until=[DateTime]::UtcNow.AddSeconds(15)
do{$after=Request snapshot;if(!@($after.activePlugins|Where-Object loading -ne loaded).Count){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $until)
Start-Sleep -Milliseconds 1000

$mixer=@($after.operating.graph.nodes|Where-Object kind -eq mixer)[0]
function Expand-Option([string]$name){(Find $name).GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand();Start-Sleep -Milliseconds 150}
Invoke ('ChainMenu-'+$mixer.id);Expand-Option 'Input channels'
if((Find 'Enable channels').Current.IsEnabled){throw 'Enable channels should be disabled when all mixer inputs are visible'}
Expand-Option 'Disable channels'
$choices=@($script:window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::MenuItem))|Where-Object {!$_.Current.IsOffscreen -and $_.Current.Name -match '\d+ Left'})
if(!$choices.Count -or @($choices|Where-Object {$_.Current.Name -match '^(Enable|Disable|Hide|Show) '}).Count){throw 'Channel names still include action prefixes'}
$channelName=$choices[0].Current.Name
$choices[0].GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke();Start-Sleep -Milliseconds 700
$until=[DateTime]::UtcNow.AddSeconds(5)
do{$after=Request snapshot;$mixer=@($after.operating.graph.nodes|Where-Object id -eq $mixer.id)[0];if(0 -in $mixer.hiddenInputs -and 1 -in $mixer.hiddenInputs){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $until)
if(0 -notin $mixer.hiddenInputs -or 1 -notin $mixer.hiddenInputs){throw 'Disable pair did not hide both inputs'}
Invoke ('ChainMenu-'+$mixer.id);Expand-Option 'Input channels';Expand-Option 'Enable channels'
Invoke $channelName
Start-Sleep -Milliseconds 700
$after=Request snapshot;$mixer=@($after.operating.graph.nodes|Where-Object id -eq $mixer.id)[0]
if(0 -in $mixer.hiddenInputs -or 1 -in $mixer.hiddenInputs){throw 'Enable pair did not restore both inputs'}
$mixer|Add-Member -NotePropertyName inputAliases -NotePropertyValue @{'0:1'='Renamed left';'1:1'='Renamed right';'2:1'='Keep other pair'} -Force
$mixer|Add-Member -NotePropertyName splitInputs -NotePropertyValue $false -Force
$layoutIndex=0;foreach($node in $after.operating.graph.nodes){$node.x=$layoutIndex*500;$node.y=100;$layoutIndex++}
Change @{action='graph';graph=$after.operating.graph};Start-Sleep -Milliseconds 700;Invoke ChainFit

Add-Type @'
using System;using System.Runtime.InteropServices;
public static class OptionsMouse {
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern void mouse_event(uint flags,uint x,uint y,uint data,UIntPtr extra);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
}
'@
[OptionsMouse]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
function Hover-Option($element){if([Windows.Automation.AutomationElement]::FocusedElement.Current.ProcessId -ne $script:uiProcess.Id){Focus-Window};$bounds=$element.Current.BoundingRectangle;if($element.Current.IsOffscreen -or $bounds.IsEmpty){throw "Hover target is outside the visible view"};[OptionsMouse]::SetCursorPos([int]($bounds.X-30),[int]($bounds.Y-30))|Out-Null;Start-Sleep -Milliseconds 100;[OptionsMouse]::SetCursorPos([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))|Out-Null;[OptionsMouse]::mouse_event(1,1,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 2600}
Hover-Option (Find ('ChainPort-'+$mixer.id+'-in-0'))
[OptionsMouse]::mouse_event(8,0,0,0,[UIntPtr]::Zero);[OptionsMouse]::mouse_event(16,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 350
Invoke 'Rename channel';Invoke 'Restore original name';Start-Sleep -Milliseconds 700
$after=Request snapshot;$mixer=@($after.operating.graph.nodes|Where-Object id -eq $mixer.id)[0]
if($mixer.inputAliases.'0:1' -or $mixer.inputAliases.'1:1' -or $mixer.inputAliases.'0:2' -or $mixer.inputAliases.'2:1' -ne 'Keep other pair'){throw 'Pair restore did not remove inherited names or affected another pair'}

function Tooltip-Count { @($script:window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ToolTip))|Where-Object {!$_.Current.IsOffscreen}).Count }
Invoke NavDiagnostics
$metric=Find Diagnostic-appCpuPercent
if(!$metric.Current.HelpText){throw 'Diagnostics lost accessible help'}
Hover-Option $metric
if(!(Tooltip-Count)){throw 'Diagnostics hover help did not appear'}
Invoke NavSettings
(Find HoverTooltipsSwitch).GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle();Start-Sleep -Milliseconds 300
Invoke NavDiagnostics;Hover-Option (Find Diagnostic-appCpuPercent)
if(Tooltip-Count){throw 'Diagnostics hover help remained enabled'}
Invoke NavSettings
(Find HoverTooltipsSwitch).GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle();Start-Sleep -Milliseconds 300
Invoke NavDiagnostics;Hover-Option (Find Diagnostic-appCpuPercent)
if(!(Tooltip-Count)){throw 'Diagnostics hover help did not return'}
$script:preserveCaptureInteraction=$true;Capture 'diagnostics-hover-help';$script:preserveCaptureInteraction=$false
@{passed=$true;multiSelect=$true;selectionSurvivesSearchAndSort=$true;channelMenus=$true;pairNameRestore=$true;hoverTooltips=$true}|ConvertTo-Json|Set-Content (Join-Path $profile 'channel-options-result.json') -Encoding UTF8
