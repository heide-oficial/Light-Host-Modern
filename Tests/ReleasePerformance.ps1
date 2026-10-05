param(
    [ValidateSet('current','baseline')][string]$Variant='current',
    [Parameter(Mandatory)][string]$OutputDevice,
    [ValidateSet('dashboard','minimized','closed')][string]$UiState='dashboard',
    [ValidateSet('normal','maximized')][string]$WindowPresentation='normal',
    [ValidateRange(0,300)][int]$WarmupSeconds=30,
    [ValidateRange(1,1800)][int]$MeasurementSeconds=300,
    [ValidateRange(1,10)][int]$Repetitions=5,
    [ValidateRange(1,8192)][int]$BufferSize=480,
    [string]$PreferencesFixture='',
    [ValidateRange(0,1000)][int]$ExpectedPlugins=0,
    [string]$ComparisonDirectory='out/bcmp1',
    [string]$OutputDirectory='out/performance'
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\HostProtocol.ps1"
$repo=(Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$root=Join-Path $repo 'out\test-profiles'
$protocol=if ($Variant -eq 'baseline') { 3 } else { 5 }
$comparison=[IO.Path]::GetFullPath((Join-Path $repo $ComparisonDirectory))
$hostExe=if ($Variant -eq 'baseline') { Join-Path $comparison 'bld\LightHostModern_artefacts\Release\LightHostModern.exe' } else { Join-Path (Get-TestBuildDirectory) 'LightHostModern_artefacts\Release\LightHostModern.exe' }
$uiDirectory=Join-Path $repo 'WinUI\x64\Release\LightHostModern.WinUI'
$baselineUi=Join-Path $repo 'out\baselines\completion-20260908-release\host\WinUI\x64\Release\LightHostModern.WinUI'
$profileFiles=@((Join-Path $env:APPDATA 'LightHostModern\LightHostModern.settings'),(Join-Path $env:LOCALAPPDATA 'LightHostModern\ui-settings.ini'))
$profileFiles+=@('','.bak','.pending','.backup-pending' | ForEach-Object { $profileFiles[0]+'.session.json'+$_ })
$profileFiles+=@('.pre-session.bak','.pre-session.bak.pending' | ForEach-Object { $profileFiles[0]+$_ })
function Production-Files {
    $hashes=@{}
    foreach ($path in $profileFiles) { $hashes[$path]=if (Test-Path -LiteralPath $path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash } else { '' } }
    return $hashes
}
$originalFiles=Production-Files
if ($Variant -eq 'baseline') {
    $adaptations=Get-Content -LiteralPath (Join-Path $comparison 'adaptations.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($adaptations.productionVersion -ne '1.2.2' -or $adaptations.protocolVersion -ne 3) { throw 'Invalid comparison adaptations.' }
    foreach ($entry in $adaptations.adaptedFiles) {
        $file=Join-Path (Join-Path $comparison 'src') $entry.path
        if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ine $entry.sha256) { throw "The comparison source differs from its reviewed adaptations: $file" }
    }
    $uiDirectory=Join-Path $comparison 'ui'
    if (!(Test-Path -LiteralPath $uiDirectory)) { Copy-Item -LiteralPath $baselineUi -Destination $uiDirectory -Recurse }
    if ((Get-FileHash -LiteralPath (Join-Path $uiDirectory 'LightHostModernWinUI.exe') -Algorithm SHA256).Hash -ne '18001958FB6B5961ECFE1EDE143573DA71452BE232C9C80CD89800ADCF646B8D') { throw 'The comparison UI is not the frozen executable.' }
}
if (!(Test-Path -LiteralPath $hostExe)) { throw 'Build the requested Release host before measuring.' }
if ($ExpectedPlugins -and !$PreferencesFixture) { throw 'Loaded-plugin scenarios require a preferences fixture.' }
if ($PreferencesFixture) { $PreferencesFixture=(Resolve-Path -LiteralPath $PreferencesFixture).Path }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$report=[ordered]@{variant=$Variant;uiState=$UiState;windowPresentation=$WindowPresentation;hostSha256=(Get-FileHash -LiteralPath $hostExe -Algorithm SHA256).Hash;
    uiSha256=(Get-FileHash -LiteralPath (Join-Path $uiDirectory 'LightHostModernWinUI.exe') -Algorithm SHA256).Hash;
    warmupSeconds=$WarmupSeconds;measurementSeconds=$MeasurementSeconds;repetitions=$Repetitions;
    expectedPlugins=$ExpectedPlugins;backend='Windows Audio';output=$OutputDevice;sampleRate=48000;bufferSize=$BufferSize;
    fixtureSha256=$(if($PreferencesFixture){(Get-FileHash -LiteralPath $PreferencesFixture -Algorithm SHA256).Hash}else{$null});
    inputOpened=$false;outputMuted=$true;logicalProcessors=[Environment]::ProcessorCount;
    fullDurationProtocol=($WarmupSeconds -ge 30 -and $MeasurementSeconds -eq 300 -and $Repetitions -eq 5);
    performanceAccepted=$false;cpuDefinition='Percent of all logical processors';gpuDefinition='Maximum utilization among the UI process GPU engines; null when unavailable';
    runs=[Collections.Generic.List[object]]::new()}
function Request([string]$Command,[array]$Arguments=@()) {
    Send-HostRequest $script:pipe $Command $Arguments -Session $script:hostSession -ProtocolVersion $protocol -TimeoutMs 10000
}
function Mutate([string]$Command,[array]$Arguments=@()) {
    $result=Request $Command $Arguments
    if ($protocol -ge 4) { $result=Wait-HostOperation $script:pipe $result -TimeoutMs 30000 }
    if ($result.status -notin @('ok','snapshot')) { throw ('Host command failed: '+($result|ConvertTo-Json -Depth 12 -Compress)) }
    return $result
}
function UI([string[]]$Arguments) {
    $result=& winapp ui @Arguments -a $script:uiPid --json
    if ($LASTEXITCODE -ne 0) { throw "UI command failed: $result" }
    return $result|ConvertFrom-Json
}
function Measurement { Request $(if ($protocol -ge 4) {'callback-measurement'}else{'comparison-measurement'}) }
function Cpu-Seconds($Process) { $Process.Refresh(); return $Process.TotalProcessorTime.TotalSeconds }
function Persist-Report { $report | ConvertTo-Json -Depth 16 | Set-Content (Join-Path $OutputDirectory 'results.json') -Encoding UTF8 }
for ($runIndex=1;$runIndex -le $Repetitions;$runIndex++) {
    $name='perf-'+$Variant+'-'+[guid]::NewGuid().ToString('N')
    $directory=Join-Path $root $name
    New-Item -ItemType Directory -Force -Path (Join-Path $directory 'Temp'),(Join-Path $directory 'EnvLocal\LightHostModern') | Out-Null
    if ($PreferencesFixture) { Copy-Item -LiteralPath $PreferencesFixture -Destination (Join-Path $directory 'LightHostModern.settings') }
    $run=[ordered]@{number=$runIndex;profile=$directory;status='running';startedUtc=[DateTime]::UtcNow.ToString('o');samples=[Collections.Generic.List[object]]::new()}
    $report.runs.Add($run)
    $hostProcess=$null; $script:uiPid=0; $script:pipe=''; $script:hostSession=''; $uiProcess=$null
    $originalEnvironment=@{LOCALAPPDATA=$env:LOCALAPPDATA;TEMP=$env:TEMP;TMP=$env:TMP}
    try {
        if ((Get-FileHash -LiteralPath $hostExe -Algorithm SHA256).Hash -ne $report.hostSha256 -or
            (Get-FileHash -LiteralPath (Join-Path $uiDirectory 'LightHostModernWinUI.exe') -Algorithm SHA256).Hash -ne $report.uiSha256 -or
            ($PreferencesFixture -and (Get-FileHash -LiteralPath $PreferencesFixture -Algorithm SHA256).Hash -ne $report.fixtureSha256)) {
            throw 'A measured artifact changed between repetitions.'
        }
        # Only this harness process and its children see these environment values.
        $env:LOCALAPPDATA=Join-Path $directory 'EnvLocal'; $env:TEMP=Join-Path $directory 'Temp'; $env:TMP=$env:TEMP
        $hostProcess=Start-Process -FilePath $hostExe -ArgumentList @("--test-profile=$name",('--profile-root="'+$root+'"')) -WindowStyle Hidden -PassThru
        $script:pipe=if ($protocol -eq 3) { '\\.\pipe\LightHostModern-'+$hostProcess.Id } else { '' }
        $deadline=[DateTime]::UtcNow.AddSeconds(180)
        do {
            try {
                if ($protocol -ge 4) { $script:pipe=(Get-Content -LiteralPath (Join-Path $directory 'profile.json') -Raw | ConvertFrom-Json).pipe }
                $initial=Request 'snapshot'
                if ($initial.status -ne 'error' -and $null -ne $initial.diagnostics) { break }
                $run.startupResponse=$initial
            } catch { $run.startupError=$_.Exception.Message }
            if ($hostProcess.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw 'The isolated performance host did not become ready.' }
            Start-Sleep -Milliseconds 100
        } while ($true)
        $script:hostSession=$initial.hostSession
        if ($initial.diagnostics.sampleRate -gt 0 -or $initial.diagnostics.loadedPlugins -ne $ExpectedPlugins) { throw 'Unexpected initial audio or loaded-plugin state.' }
        if ($protocol -eq 3) {
            $suspended=Measurement
            if ($suspended.driverAvailable -or $suspended.testProfile -ne $name) { throw 'The comparison copy is not isolated or already opened a device.' }
            # Exercise the old watchdog before arming. It must not open defaults.
            Start-Sleep -Seconds 6
            if ((Measurement).driverAvailable) { throw 'The comparison watchdog opened a default device.' }
        }
        if ($UiState -ne 'closed') {
            $launch=Start-TestUi -Directory $uiDirectory -Arguments @("--test-profile=$name", "--profile-root=$root", "--host-pipe=$script:pipe")
            if ($LASTEXITCODE -ne 0 -or !$launch.ProcessId) { throw 'The performance UI did not start.' }
            $script:uiPid=$launch.ProcessId; $uiProcess=Get-Process -Id $script:uiPid
            $run.uiActualPath=$uiProcess.Path
            $run.uiActualSha256=(Get-FileHash -LiteralPath $run.uiActualPath -Algorithm SHA256).Hash
            if ($run.uiActualSha256 -ne $report.uiSha256) { throw 'The launched UI differs from the measured artifact.' }
            UI @('wait-for','NavDashboard','-t','15000') | Out-Null
            UI @('invoke','NavDashboard') | Out-Null
            if ($WindowPresentation -eq 'maximized') { UI @('invoke','Maximize') | Out-Null }
            $run.window=UI @('inspect','-d','0')
            UI @('screenshot','-o',(Join-Path $OutputDirectory "run-$runIndex-dashboard.png")) | Out-Null
            if ($UiState -eq 'minimized') { UI @('invoke','Minimize') | Out-Null }
        }
        foreach ($key in $originalEnvironment.Keys) { [Environment]::SetEnvironmentVariable($key,$originalEnvironment[$key],'Process') }
        if ($protocol -ge 4) {
            Mutate 'set-global-mute' @($true) | Out-Null
            Mutate 'measure-callbacks' @($WarmupSeconds,$MeasurementSeconds) | Out-Null
            $selection=@{backend='Windows Audio';input='';output=$OutputDevice;inputMask='0';outputMask='11';defaultInputChannels=$false;defaultOutputChannels=$false;sampleRate=48000;bufferSize=$BufferSize;expectedGeneration=[string]$initial.audioSelection.generation}
            Mutate 'select-audio-device' @($selection) | Out-Null
        } else {
            $configuration=@{output=$OutputDevice;warmup=$WarmupSeconds;duration=$MeasurementSeconds;sampleRate=48000;bufferSize=$BufferSize}|ConvertTo-Json -Compress
            Mutate 'comparison-configure' @($configuration) | Out-Null
        }
        $opened=Request 'snapshot'; $run.openedDiagnostics=$opened.diagnostics
        if (!$opened.globalMuted -or $opened.diagnostics.inputChannels -ne 0 -or $opened.diagnostics.outputChannels -ne 2 -or $opened.diagnostics.sampleRate -ne 48000 -or $opened.diagnostics.bufferSize -ne $BufferSize) { throw 'The actual driver configuration differs from the requested comparison.' }
        $run.transportBefore=if ($protocol -ge 4) { Request 'transport-diagnostics' } else { $null }
        $lastClock=[Diagnostics.Stopwatch]::GetTimestamp(); $lastHost=Cpu-Seconds $hostProcess; $lastUi=if ($uiProcess) { Cpu-Seconds $uiProcess }else{0}
        $deadline=[DateTime]::UtcNow.AddSeconds($WarmupSeconds+$MeasurementSeconds+30)
        $lastProgress=[DateTime]::UtcNow
        $gpu=$null; $gpuAvailable=$false; $gpuError=''; $lastGpu=[DateTime]::MinValue
        do {
            $measurement=Measurement
            if ($measurement.phase -eq 'completed') { break }
            if ($measurement.phase -eq 'interrupted' -or [DateTime]::UtcNow -ge $deadline -or $hostProcess.HasExited -or ($uiProcess -and $uiProcess.HasExited)) { throw 'The performance run was interrupted or timed out.' }
            $now=[Diagnostics.Stopwatch]::GetTimestamp(); $interval=($now-$lastClock)/[double][Diagnostics.Stopwatch]::Frequency
            $hostCpu=Cpu-Seconds $hostProcess; $uiCpu=if ($uiProcess) { Cpu-Seconds $uiProcess }else{0}
            if ($uiProcess -and ([DateTime]::UtcNow-$lastGpu).TotalSeconds -ge 5) {
                try {
                    $engines=@(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine -Filter "Name LIKE 'pid_$($script:uiPid)_%'" -OperationTimeoutSec 5)
                    $gpuAvailable=$engines.Count -gt 0; $gpu=if ($gpuAvailable) { ($engines|Measure-Object UtilizationPercentage -Maximum).Maximum }else{$null}
                } catch { $gpu=$null; $gpuAvailable=$false; $gpuError=$_.Exception.Message }
                $lastGpu=[DateTime]::UtcNow
            }
            $workers=@(Get-Process -Name LightHostModernScanner,LightHostModernUpdateHelper -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith((Split-Path $hostExe -Parent)+'\',[StringComparison]::OrdinalIgnoreCase) })
            $run.samples.Add([pscustomobject]@{phase=$measurement.phase;utc=[DateTime]::UtcNow.ToString('o');intervalSeconds=$interval;
                hostCpuPercent=100*($hostCpu-$lastHost)/$interval/[Environment]::ProcessorCount;uiCpuPercent=$(if($uiProcess){100*($uiCpu-$lastUi)/$interval/[Environment]::ProcessorCount}else{$null});
                hostPrivateBytes=$hostProcess.PrivateMemorySize64;hostWorkingSetBytes=$hostProcess.WorkingSet64;
                uiPrivateBytes=$(if($uiProcess){$uiProcess.PrivateMemorySize64}else{$null});uiWorkingSetBytes=$(if($uiProcess){$uiProcess.WorkingSet64}else{$null});
                gpuPercent=$gpu;gpuAvailable=$gpuAvailable;gpuSampleAgeSeconds=([DateTime]::UtcNow-$lastGpu).TotalSeconds;
                workers=@($workers | ForEach-Object { @{pid=$_.Id;cpuSeconds=$_.CPU;privateBytes=$_.PrivateMemorySize64} });
                callbacks=$measurement.callbacks;samples=$measurement.samples})
            $lastClock=$now; $lastHost=$hostCpu; $lastUi=$uiCpu
            if (([DateTime]::UtcNow-$lastProgress).TotalSeconds -ge 60) {
                Write-Host "PROGRESS: $Variant/$UiState run $runIndex/$Repetitions, $($measurement.phase), $($measurement.callbacks) callbacks / $($measurement.samples) samples."
                $lastProgress=[DateTime]::UtcNow
            }
            Start-Sleep -Milliseconds 900
        } while ($true)
        $final=Request 'snapshot'; $run.measurement=$measurement; $run.finalDiagnostics=$final.diagnostics; $run.gpuError=$gpuError
        $run.transportAfter=if ($protocol -ge 4) { Request 'transport-diagnostics' } else { $null }
        if ((Get-FileHash -LiteralPath $hostExe -Algorithm SHA256).Hash -ne $report.hostSha256 -or
            (Get-FileHash -LiteralPath (Join-Path $uiDirectory 'LightHostModernWinUI.exe') -Algorithm SHA256).Hash -ne $report.uiSha256) { throw 'A measured artifact changed during the run.' }
        if ($uiProcess -and (Get-FileHash -LiteralPath $run.uiActualPath -Algorithm SHA256).Hash -ne $run.uiActualSha256) { throw 'The launched UI changed during the run.' }
        $run.processedWork=if ($protocol -ge 4) { @{blocks=$final.diagnostics.processedBlocks;samples=$final.diagnostics.processedSamples;midiInputEvents=$final.diagnostics.inputMidiEvents;midiOutputEvents=$final.diagnostics.outputMidiEvents} } else { @{blocks=$measurement.processedBlocks;samples=$measurement.processedSamples;midiInputEvents=$measurement.midiInputEvents;midiOutputEvents=$null} }
        if (!$measurement.hostAllocationAuditAvailable -or $measurement.thirdPartyAllocationAuditAvailable) { throw 'The callback audit coverage is unavailable or misreported.' }
        if ([uint64]$measurement.callbacks -eq 0 -or [uint64]$measurement.samples -ne [uint64]$measurement.callbacks*$BufferSize -or [uint64]$measurement.samples -gt [uint64]$run.processedWork.samples) { throw 'Delivered/processed work counters disagree.' }
        if ($final.diagnostics.loadedPlugins -ne $ExpectedPlugins -or $final.diagnostics.processFailures -ne 0) { throw 'Processing was lost or failed.' }
        if ($Variant -eq 'current' -and ([uint64]$measurement.hostAllocations -ne 0 -or [uint64]$measurement.hostFrees -ne 0)) { throw 'The final host allocated or freed memory in its callback.' }
        $run.status='measured'; $run.xRuns=$final.diagnostics.xRunCount
        Write-Host "MEASURED: $Variant/$UiState run $runIndex/$Repetitions, $($measurement.callbacks) callbacks, xruns $($run.xRuns), host alloc/free $($measurement.hostAllocations)/$($measurement.hostFrees)."
    } catch { $run.status='failed'; $run.error=$_.Exception.Message; Write-Host "FAIL: $Variant/$UiState run $runIndex : $($run.error)" }
    finally {
        foreach ($key in $originalEnvironment.Keys) { [Environment]::SetEnvironmentVariable($key,$originalEnvironment[$key],'Process') }
        try {
            if ($uiProcess -and !$uiProcess.HasExited) { UI @('invoke','Close')|Out-Null; if (!$uiProcess.WaitForExit(15000)) { throw 'The performance UI did not close.' } }
            if ($hostProcess -and !$hostProcess.HasExited) {
                try { Request 'quit-host'|Out-Null }
                catch { $run.quitResponseError=$_.Exception.Message }
                if (!$hostProcess.WaitForExit(30000)) { throw 'The performance host did not finish its session save and exit.' }
            }
        } catch { $run.status='failed'; $run.shutdownError=$_.Exception.Message }
        $after=Production-Files
        $changed=@($originalFiles.Keys | Where-Object { $originalFiles[$_] -ne $after[$_] })
        $run.productionPreferencesUnchanged=$changed.Count -eq 0
        if ($changed.Count) { $run.status='failed'; $run.isolationError='Production preferences changed during the run.' }
        $run.finishedUtc=[DateTime]::UtcNow.ToString('o'); Persist-Report
    }
    if ($run.status -eq 'failed') { break }
}
if (@($report.runs | Where-Object status -eq 'failed').Count) { exit 1 }
