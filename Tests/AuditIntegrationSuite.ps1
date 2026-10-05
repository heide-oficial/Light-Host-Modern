param(
    [string]$HostExecutable="$PSScriptRoot/../out/build/windows-vs2022/LightHostModern_artefacts/Release/LightHostModern.exe",
    [string]$OutputDirectory='out/audit-integration',
    [switch]$IncludeUi,
    [switch]$IncludeHardware,
    [string]$AsioDevice='',
    [string]$PackageDirectory=''
)
$ErrorActionPreference='Stop'
$hostPath=(Resolve-Path -LiteralPath $HostExecutable).Path
$output=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force|Out-Null
$results=[Collections.Generic.List[object]]::new()
function Run([string]$Script,[string[]]$Arguments=@()) {
    $clock=[Diagnostics.Stopwatch]::StartNew()
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot $Script))+@($Arguments)
    $quoted=@($arguments|ForEach-Object {'"'+$_.Replace('"','\"')+'"'})
    $process=Start-Process powershell -ArgumentList $quoted -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $output ($Script+'.log')) -RedirectStandardError (Join-Path $output ($Script+'.error.log'))
    $null=$process.Handle
    if(!$process.WaitForExit(300000)){Stop-Process -Id $process.Id -Force;throw "Integration script exceeded its five-minute deadline: $Script"}
    $code=$process.ExitCode; $clock.Stop()
    $results.Add(@{test=$Script;passed=($code -eq 0);exitCode=$code;durationMs=$clock.ElapsedMilliseconds})
    $results|ConvertTo-Json -Depth 6|Set-Content (Join-Path $output 'results.json') -Encoding UTF8
    Write-Output "$Script : exit $code ($($clock.ElapsedMilliseconds) ms)"
}
Run CheckPowerShellSyntax.ps1
foreach($script in @('ProfileIntegrationTests.ps1','InstanceIntegrationTests.ps1','SessionIntegrationTests.ps1',
    'StateIntegrationTests.ps1','ScanIntegrationTests.ps1','ProfileEditIsolationTests.ps1','MixerIntegrationTests.ps1','IsolatedHostIntegrationTests.ps1')){
    Run $script @('-HostExecutable',$hostPath)
}
Run PluginPreferencesIntegrationTests.ps1 @('-HostExecutable',$hostPath,'-FixtureExecutable',"$PSScriptRoot/../out/build/windows-vs2022/Release/LightHostModernPluginInstanceTests.exe",'-OutputDirectory','out/test-profiles')
if($IncludeUi){
    Run ChainAuditUiTests.ps1 @('-EntryPoint',$hostPath,'-ExerciseDeviceModal')
    Run UiLifetimeIntegrationTests.ps1 @('-HostExecutable',$hostPath,'-OutputDirectory',(Join-Path $output 'ui-lifetime'))
    Run VerboseLogUiIntegrationTests.ps1 @('-HostExecutable',$hostPath,'-OutputDirectory',(Join-Path $output 'logs-ui'))
    Run ModernizationUiSmoke.ps1 @('-PayloadDirectory',(Split-Path -Parent $hostPath),'-SimulatedAudio')
}
if($IncludeHardware){
    # Ordinary distributable builds do not patch their CRT imports. Allocation
    # audit is enforced by the instrumented native audio/worker tests; this
    # driver check records the host's actual coverage without claiming it.
    Run CallbackMeasurementIntegrationTests.ps1 @('-OutputDirectory',(Join-Path $output 'callbacks'))
    Run HardwareIntegrationTests.ps1 @('-OutputDirectory',(Join-Path $output 'hardware'))
    $audioArguments=@('-HostExecutable',$hostPath,'-OutputDirectory',(Join-Path $output 'isolated-audio'))
    if($AsioDevice){$audioArguments+=@('-AsioDevice',$AsioDevice)}
    else{$audioArguments+=@('-Backends','Windows Audio')}
    Run IsolatedAudioIntegrationTests.ps1 $audioArguments
}
if($PackageDirectory){Run PackageInspectionTests.ps1 @('-PackageDirectory',[IO.Path]::GetFullPath($PackageDirectory))}
if(@($results|Where-Object {!$_.passed}).Count){exit 1}
