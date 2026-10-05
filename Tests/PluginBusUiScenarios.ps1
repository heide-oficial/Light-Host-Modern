# Run inside ChainAuditUiTests' isolated profile; no pointer gestures required.
param([string]$InstanceId='')
$script:preserveCaptureInteraction=$false
Invoke NavProfiles
& "$repo/Utilities/Capture UI Review.ps1" -UiPid $script:uiProcess.Id -PrepareOnly -OutputDirectory $output
Invoke 'Save current settings as a new profile'
if([Windows.Automation.AutomationElement]::FocusedElement.Current.ProcessId -ne $script:uiProcess.Id){
 Write-Output 'Another application took desktop focus; reopening the dialog with the isolated window in front.'
 Invoke Cancel
 Focus-Window
 Invoke 'Save current settings as a new profile'
}
if(!(Find ProfileName).Current.HasKeyboardFocus){
 $focused=[Windows.Automation.AutomationElement]::FocusedElement.Current
 throw "New profile does not focus Name: $($focused.AutomationId) / $($focused.Name) / process $($focused.ProcessId)"
}
Capture 'new-profile-name-focus'
Invoke Cancel
Invoke NavPlugins
if(!$InstanceId){
 $fixture=Join-Path $repo 'out/build/windows-vs2022/scn/LHFixture_artefacts/Release/VST3/LightHostModern Scenario Fixture.vst3'
 if(!(Test-Path -LiteralPath $fixture)){throw 'Build LHFixture_VST3 before the channel regression'}
 Resolve (Request begin-plugin-scan)|Out-Null
 Resolve (Request scan-plugin-path @($fixture))|Out-Null
 $until=[DateTime]::UtcNow.AddSeconds(30)
 while((Request plugin-scan-status).active){if([DateTime]::UtcNow -gt $until){throw 'Fixture scan timed out'};Start-Sleep -Milliseconds 100}
 $known=@((Request snapshot).knownPluginList)[0]
 if(!$known.knownId){throw 'Test fixture was not discovered'}
 Resolve (Request add-known-plugin @($known.knownId))|Out-Null
 $until=[DateTime]::UtcNow.AddSeconds(15)
 do{$state=Request snapshot;$plugin=@($state.activePlugins)[0];if($plugin.loading -eq 'loaded'){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $until)
 if($plugin.loading -ne 'loaded'){throw 'Test fixture did not load'}
 $InstanceId=$plugin.instanceId
}
$inventory=Request plugin-buses @($InstanceId)
$inventory|ConvertTo-Json -Depth 64|Set-Content (Join-Path $profile 'plugin-bus-inventory.json') -Encoding UTF8
if(!@($inventory.inputs[0].choices|Where-Object name -eq Mono).Count){throw 'Supported coupled mono layout is missing'}
$monoOption=@($inventory.inputs[0].choices|Where-Object name -eq Mono)[0]
if(@($monoOption.layout.inputs[0]).Count -ne 1 -or @($monoOption.layout.outputs[0]).Count -ne 1){throw 'Inactive VST3 layout negotiation did not couple input and output'}
if(@($inventory.inputs[0].choices|Where-Object name -eq Quadraphonic).Count){throw 'Unsupported VST3 layout was offered'}
$beforeBuses=Request snapshot
$mixers=@($beforeBuses.operating.graph.nodes|Where-Object kind -eq mixer)
$a=$mixers[0];$b=$mixers[1]
$a.x=0;$a.y=100;$b.x=900;$b.y=100
$pluginNode=@($beforeBuses.operating.graph.nodes|Where-Object id -eq $InstanceId)[0]
$pluginNode.x=450;$pluginNode.y=100
$pluginNode|Add-Member -NotePropertyName splitInputs -NotePropertyValue $false -Force
$pluginNode|Add-Member -NotePropertyName splitOutputs -NotePropertyValue $false -Force
$beforeBuses.operating.graph.edges=@(
 @{id=[guid]::NewGuid().ToString('N');from=$a.id;to=$InstanceId;output=0;input=0;sourceWidth=2;targetWidth=2},
 @{id=[guid]::NewGuid().ToString('N');from=$InstanceId;to=$b.id;output=0;input=0;sourceWidth=2;targetWidth=2}
)
Change @{action='graph';graph=$beforeBuses.operating.graph}
Start-Sleep -Milliseconds 700
Invoke ChainFit
& "$repo/Utilities/Capture UI Review.ps1" -UiPid $script:uiProcess.Id -PrepareOnly -OutputDirectory $output
Invoke ('ChainMenu-'+$InstanceId)
Start-Sleep -Milliseconds 800
$menuItems=@($script:window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::MenuItem))|Where-Object {!$_.Current.IsOffscreen}|ForEach-Object {$_.Current.Name})
$configureIndex=[Array]::IndexOf($menuItems,'Configure plugin channels')
if($configureIndex -lt 0 -or $menuItems[$configureIndex+1] -ne 'Input channels'){throw 'Plugin channel configuration is not above Input channels'}
Invoke 'Configure plugin channels'
$bus=Find 'PluginInputBus-0'
$bus.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
Start-Sleep -Milliseconds 250
$options=@($bus.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem))|Where-Object {$_.Current.Name -in @('Stereo','Mono','Disabled')}|ForEach-Object {$_.Current.Name})
$expectedOptions=@('Stereo','Mono','Disabled'|Where-Object {$_ -in @($inventory.inputs[0].choices.name)})
if(($options -join '|') -ne ($expectedOptions -join '|')){throw "Plugin format choices are in the wrong order: $options"}
$items=$script:window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'Mono'))
$item=@($items|Where-Object {!$_.Current.IsOffscreen})[0]
if(!$item){throw 'Mono choice is not accessible'}
$item.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
Start-Sleep -Milliseconds 300
Capture 'plugin-audio-channels-mono'
Invoke PluginOutputChannelsTab
$outputBus=Find 'PluginOutputBus-0'
if($outputBus.Current.IsOffscreen){throw 'Output tab did not show its channel controls'}
$outputSelection=@($outputBus.GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection())
if(!$outputSelection.Count -or $outputSelection[0].Current.Name -ne 'Mono'){throw 'Changing input format did not retain the coupled output format across tabs'}
Capture 'plugin-audio-output-tab'
Invoke PluginInputChannelsTab
$inputSelection=@((Find 'PluginInputBus-0').GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection())
if(!$inputSelection.Count -or $inputSelection[0].Current.Name -ne 'Mono'){throw 'Switching tabs lost the chosen input format'}
Invoke Apply
$until=[DateTime]::UtcNow.AddSeconds(10)
do{$state=Request snapshot;$n=@($state.operating.graph.nodes|Where-Object id -eq $InstanceId)[0];if(@($n.inputPorts|Where-Object {$_.type -eq 3 -and $_.physical -eq 0}).Count){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $until)
$state|ConvertTo-Json -Depth 64|Set-Content (Join-Path $profile 'plugin-bus-applied.json') -Encoding UTF8
if($n.inputPorts[0].physical -ne -1 -or $n.outputPorts[0].physical -ne -1 -or @($state.operating.graph.edges).Count -ne 2){throw 'Mono change redirected or deleted existing L/R wires'}
Capture 'plugin-unavailable-stereo-wires'
$monoInventory=Request plugin-buses @($InstanceId)
if(@($monoInventory.layout.inputs[0]).Count -ne 1 -or @($monoInventory.layout.outputs[0]).Count -ne 1){throw 'Coupled mono layout was not applied'}
Change @{action='plugin-buses';id=$InstanceId;layout=$inventory.layout;previousLayout=$monoInventory.layout}
Start-Sleep -Milliseconds 700
$state=Request snapshot;$n=@($state.operating.graph.nodes|Where-Object id -eq $InstanceId)[0]
if($n.inputPorts[0].physical -ne 0 -or $n.outputPorts[1].physical -ne 1 -or @($state.operating.graph.edges).Count -ne 2){throw 'Restoring stereo did not restore saved channels'}
Capture 'plugin-stereo-wires-restored'
# Exercise the same actual VST3 through the separate-process metadata path.
Change @{action='isolation';id=$InstanceId;enabled=$true}
$until=[DateTime]::UtcNow.AddSeconds(25)
do{$state=Request snapshot;$p=@($state.activePlugins|Where-Object instanceId -eq $InstanceId)[0];if($p.loading -eq 'loaded'){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $until)
if($p.loading -ne 'loaded'){throw 'Isolated VST3 did not load'}
$remoteInventory=Request plugin-buses @($InstanceId)
$remoteMono=@($remoteInventory.inputs[0].choices|Where-Object name -eq Mono)[0]
if(!$remoteMono -or @($remoteMono.layout.outputs[0]).Count -ne 1 -or @($remoteInventory.inputs[0].choices|Where-Object name -eq Quadraphonic).Count){throw 'Isolated VST3 advertised unsupported layouts'}
Change @{action='plugin-buses';id=$InstanceId;layout=$remoteMono.layout;previousLayout=$remoteInventory.layout}
$until=[DateTime]::UtcNow.AddSeconds(15)
do{$state=Request snapshot;$n=@($state.operating.graph.nodes|Where-Object id -eq $InstanceId)[0];if(@($n.inputPorts|Where-Object {$_.type -eq 3 -and $_.physical -eq 0}).Count){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $until)
if($n.inputPorts[0].physical -ne -1 -or $n.outputPorts[0].physical -ne -1 -or @($state.operating.graph.edges).Count -ne 2){throw 'Isolated mono change lost channel identity'}
$current=Request plugin-buses @($InstanceId)
Change @{action='plugin-buses';id=$InstanceId;layout=$remoteInventory.layout;previousLayout=$current.layout}
$until=[DateTime]::UtcNow.AddSeconds(15)
do{$state=Request snapshot;$n=@($state.operating.graph.nodes|Where-Object id -eq $InstanceId)[0];if($n.inputPorts[0].physical -eq 0 -and $n.outputPorts[1].physical -eq 1){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $until)
if($n.inputPorts[0].physical -ne 0 -or $n.outputPorts[1].physical -ne 1 -or @($state.operating.graph.edges).Count -ne 2){throw 'Isolated stereo did not restore saved channels'}
$catalogEntry=@((Request snapshot).knownPluginList)[0]
Resolve (Request rename-known-plugin @($catalogEntry.knownId,'Temporary fixture name'))|Out-Null
Start-Sleep -Milliseconds 800
& "$repo/Utilities/Capture UI Review.ps1" -UiPid $script:uiProcess.Id -PrepareOnly -OutputDirectory $output
Invoke 'Add plugin';Invoke $catalogEntry.knownId
Start-Sleep -Milliseconds 800
$menuItems=@($script:window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::MenuItem))|Where-Object {!$_.Current.IsOffscreen}|ForEach-Object {$_.Current.Name})
if('Restore original name' -in $menuItems -or 'Rename plugin' -notin $menuItems){throw "Unexpected catalog menu: $($menuItems -join ' | ')"}
Invoke 'Rename plugin'
Capture 'plugin-rename-with-restore'
Invoke 'Restore original name'
$until=[DateTime]::UtcNow.AddSeconds(5)
do{$details=Request known-plugin-details @($catalogEntry.knownId);if(!$details.customName){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $until)
if($details.customName){throw 'Restore original name did not clear the custom name'}
Invoke Cancel
@{passed=$true;profileNameFocus=$true;renameRestoreDialog=$true;pluginFormatOrder=$true;pluginBusDialog=$true;coupledMono=$true;unavailableWires=$true;stereoRestored=$true;isolatedVst3Layouts=$true}|
 ConvertTo-Json|Set-Content (Join-Path $profile 'plugin-bus-result.json') -Encoding UTF8
