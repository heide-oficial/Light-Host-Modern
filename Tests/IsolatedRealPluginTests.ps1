param(
    [Parameter(Mandatory)][string[]]$PluginPaths,
    [string]$Format='VST3',
    [string]$BuildDirectory="$PSScriptRoot/../out/build/windows-vs2022",
    [int]$TimeoutSeconds=120
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath("$PSScriptRoot/..")
$output=Join-Path $repo ('out/isolated-real-plugins/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output -Force|Out-Null
$runner=Join-Path $BuildDirectory 'Release/LightHostModernIsolatedRealPluginTests.exe'
$worker=Join-Path $BuildDirectory 'LightHostModern_artefacts/Release/LightHostModernWorker.exe'
$results=@();$index=0
foreach($module in $PluginPaths){
    $index++;$report=Join-Path $output "$index.json"
    $p=Start-Process $runner -ArgumentList @(('"'+$worker+'"'),$Format,('"'+[IO.Path]::GetFullPath($module)+'"'),('"'+$report+'"')) -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit($TimeoutSeconds*1000)){Stop-Process -Id $p.Id -Force; throw "Plugin test timed out: $module"}
    if(!(Test-Path -LiteralPath $report)){throw "Plugin test crashed before report: $module"}
    $result=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json; $results+=$result
    $result|ConvertTo-Json -Depth 8
}
$results|ConvertTo-Json -Depth 8|Set-Content (Join-Path $output 'results.json') -Encoding UTF8
if(@($results|Where-Object { !$_.passed }).Count){throw "Real-plugin failures. Evidence: $output"}
Write-Output "PASS real-plugin isolated editor/audio/state/restore: $output"
