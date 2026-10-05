# Capture a running isolated development instance through Windows UI Automation.
# This helper navigates and captures only; it does not run test suites or launch audio.
# Example: powershell -ExecutionPolicy Bypass -File "Utilities/Capture UI Review.ps1" -UiPid 1234 -AllPages
param([int]$UiPid=0,[string]$Page='',[string]$Invoke='',[string]$Select='',[string]$Value='', [string]$Name='capture',[int]$Width=0,[int]$Height=0,[switch]$AllPages,[switch]$PrepareOnly,[switch]$IncludeNativeMenus,[switch]$PreserveInteraction,[switch]$Passive,[string]$OutputDirectory="")
$ErrorActionPreference='Stop'
if (!$OutputDirectory) { $OutputDirectory=Join-Path $PSScriptRoot '../out/ui-review/captures' }
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class ReviewWindow {
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern void keybd_event(byte k,byte s,uint f,UIntPtr e);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr v);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint f);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int z,uint f);
}
'@
[ReviewWindow]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))|Out-Null
if(!$UiPid){throw 'Pass -UiPid with the isolated review UI process ID.'}
$process=Get-Process -Id $UiPid
if(!$process -or !$process.MainWindowTitle.Contains('[Test:')) {throw 'An isolated review window is required.'}
Write-Output $process.MainWindowTitle
$window=[Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
if($Passive){
 # Render a static view without changing focus or injecting keyboard input.
}elseif($PreserveInteraction){
 # Capturing a drag/hover must not inject Alt or reactivate the window: doing
 # either can cancel pointer capture and turn the observation into the cause.
 if([ReviewWindow]::GetForegroundWindow() -ne $process.MainWindowHandle){throw 'The isolated gesture window is not foreground.'}
}else{
 [ReviewWindow]::ShowWindow($process.MainWindowHandle,9)|Out-Null
 Start-Sleep -Milliseconds 400
 [ReviewWindow]::SetForegroundWindow($process.MainWindowHandle)|Out-Null
 [ReviewWindow]::keybd_event(18,0,0,[UIntPtr]::Zero)
 [ReviewWindow]::keybd_event(18,0,2,[UIntPtr]::Zero)
 $activation=New-Object -ComObject WScript.Shell
 if(!$activation.AppActivate($process.Id)){throw 'Cannot activate the isolated review window.'}
 Start-Sleep -Milliseconds 250
}
if($Width -and $Height){[ReviewWindow]::SetWindowPos($process.MainWindowHandle,[IntPtr]::Zero,60,60,$Width,$Height,4)|Out-Null;Start-Sleep -Milliseconds 500}
function Find([string]$key) {
 $el=$window.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$key))
 if(!$el){$el=$window.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$key))}
 if(!$el){throw "Control not found: $key"};return $el
}
function Click([string]$key){
 $el=Find $key;$p=$null
 if($el.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$p)){$p.Invoke()}
 elseif($el.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$p)){$p.Select()}
 else{throw "Cannot invoke $key"}
 Start-Sleep -Milliseconds 500
}
function Capture([string]$file){
 Start-Sleep -Milliseconds 600
 $bounds=$window.Current.BoundingRectangle
 $bitmap=[Drawing.Bitmap]::new([int]$bounds.Width,[int]$bounds.Height)
 # Native menu popups are omitted by PrintWindow. During gestures avoid
 # WM_PRINT as well: observe the foreground pixels without asking WinUI to paint.
 $g=[Drawing.Graphics]::FromImage($bitmap)
 if($IncludeNativeMenus -or $PreserveInteraction){
  if([ReviewWindow]::GetForegroundWindow() -ne $process.MainWindowHandle){$g.Dispose();$bitmap.Dispose();throw 'The review window lost focus; capture cancelled.'}
  try{$g.CopyFromScreen([int]$bounds.X,[int]$bounds.Y,0,0,$bitmap.Size)}finally{$g.Dispose()}
 }else{
  $dc=$g.GetHdc();try{$ok=[ReviewWindow]::PrintWindow($process.MainWindowHandle,$dc,2)}finally{$g.ReleaseHdc($dc);$g.Dispose()}
  if(!$ok){$bitmap.Dispose();throw 'PrintWindow failed.'}
 }
 try{$bitmap.Save((Join-Path $OutputDirectory "$file.png"),[Drawing.Imaging.ImageFormat]::Png)}finally{$bitmap.Dispose()}
 # Popups/tooltips may disappear during enumeration; the PNG remains authoritative.
 try{$elements=$window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)}catch [Windows.Automation.ElementNotAvailableException]{$elements=@()}
 $lines=foreach($e in $elements){try{$c=$e.Current;if(!$c.IsOffscreen){'{0} | {1} | {2} | {3}' -f $c.ControlType.ProgrammaticName,$c.AutomationId,$c.Name,$c.BoundingRectangle}}catch [Windows.Automation.ElementNotAvailableException]{}}
 $lines|Set-Content -Encoding UTF8 (Join-Path $OutputDirectory "$file.txt")
 Write-Output "Captured $file.png (PID $($process.Id))"
}
if($PrepareOnly){return}
if($AllPages){foreach($p in @('Plugins','Profiles','Settings')){Click "Nav$p";Capture "$Name-$p"}}
else{
 if($Page){Click "Nav$Page"}
 if($Select){$box=Find $Select;$box.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand();Start-Sleep -Milliseconds 200;Click $Value}
 if($Invoke){Click $Invoke}
 Capture $Name
}
