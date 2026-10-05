param(
    [Parameter(Mandatory)][string] $PackageDirectory,
    [string] $ExpectedVersion = '2.0.0',
    [string] $BaselineHelper = '',
    [string] $OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/HostProtocol.ps1"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$packages = (Get-Item -LiteralPath $PackageDirectory).FullName
if (!$BaselineHelper) { $BaselineHelper = Join-Path (Get-TestBuildDirectory) 'test-fixtures/Release/LightHostModernUpdateHelperBaselineFixture.exe' }
$fixtureHelper = (Get-Item -LiteralPath $BaselineHelper).FullName
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repo ('out/portable-update-integration-' + [guid]::NewGuid().ToString('N')) }
$work = [IO.Path]::GetFullPath($OutputDirectory)
$outRoot = [IO.Path]::GetFullPath((Join-Path $repo 'out')).TrimEnd('\') + '\'
if (!$work.StartsWith($outRoot, [StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $work)) { throw 'Use a new workspace out directory for the portable update fixture.' }
function No-Reparse([string] $Path) {
    for ($part = [IO.Path]::GetFullPath($Path); $part; $part = [IO.Path]::GetDirectoryName($part)) {
        if ((Test-Path -LiteralPath $part) -and ((Get-Item -LiteralPath $part).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Reparse point in fixture path: $part" }
    }
}
No-Reparse $work
No-Reparse $packages
if ([version]$ExpectedVersion -le [version]'1.9.9') { throw 'The signed candidate must be newer than fixture baseline 1.9.9.' }
New-Item -ItemType Directory -Path $work | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
$install = Join-Path $work 'portable'
$operation = Join-Path $work 'operation'
$profileRoot = Join-Path $work 'profiles'
$profileName = 'portable-update-' + [guid]::NewGuid().ToString('N')
$testProfileDirectory = Join-Path $profileRoot $profileName
New-Item -ItemType Directory -Path $operation,$testProfileDirectory | Out-Null
[IO.File]::WriteAllText((Join-Path $work 'FIXTURE-ONLY.txt'), 'Never publish. Baseline uses unchanged current release binaries with synthetic initial inventory version 1.9.9. Candidate ZIP and signatures are exact release artifacts. This does not certify historical-version migration or power-loss recovery.', $utf8)
$results = [Collections.Generic.List[object]]::new()
$helperProcess = $null; $hostProcess = $null; $uiProcess = $null
$arm = $null; $cancel = $null; $ready = $null
$pipe = ''; $session = ''; $failure = $null
$marker = [guid]::NewGuid().ToString('N')
$settings = Join-Path $testProfileDirectory 'LightHostModern.settings'
$markerPath = Join-Path $testProfileDirectory 'retained-user-data.bin'
[IO.File]::WriteAllText($settings, ('<PROPERTIES><VALUE name="portableUpdateSentinel" val="' + $marker + '"/><VALUE name="closeBehavior" val="quit"/></PROPERTIES>'), $utf8)
[IO.File]::WriteAllText($markerPath, $marker, $utf8)
[IO.File]::WriteAllText((Join-Path $testProfileDirectory 'ui-settings.ini'), ("[Localization]`r`nLanguage=en-us`r`n[UpdateTestSentinel]`r`nToken=" + $marker + "`r`n"), $utf8)
function Assert([bool] $Condition, [string] $Message) { if (!$Condition) { throw $Message } }
function Passed([string] $Name) { $results.Add([ordered]@{ name=$Name; status='passed' }); Write-Host "PASS: $Name" }
function Hash([string] $Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Json-Write([string] $Path, $Value) { [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 30 -Compress), $utf8) }
function Start-Hidden([string] $Executable, [string[]] $Arguments) {
    # Arguments used here contain no embedded quotes or trailing directory slash.
    $quoted = @($Arguments | ForEach-Object { if ($_ -match '["\r\n]') { throw 'Unsafe process argument' }; '"' + $_ + '"' })
    $p = Start-Process -FilePath $Executable -ArgumentList $quoted -WorkingDirectory $work -WindowStyle Hidden -PassThru
    $null = $p.Handle
    return $p
}
function Result-State { (Get-Content -LiteralPath (Join-Path $operation 'update-result.json') -Raw | ConvertFrom-Json).state }
function Helper-Arguments([string] $Mode) {
    @('--mode',$Mode,'--operation',$operation,'--package',$package,'--distribution','portable',
      '--version',('v'+$ExpectedVersion),'--size',[string]$artifact.size,'--sha256',[string]$artifact.digest)
}
function Read-State {
    $valid = @()
    foreach ($slot in 0,1) {
        $path = Join-Path $install "portable-state-$slot.json"
        if (!(Test-Path -LiteralPath $path)) { continue }
        $envelope = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $digest = [BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes($envelope.body))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
        Assert ($digest -eq $envelope.sha256) 'Portable state checksum is invalid'
        $valid += $envelope.body | ConvertFrom-Json
    }
    if (!$valid.Count) { return [pscustomobject]@{ sequence=0; confirmed=$baseline; previous=$null; candidate=$null; launching=$false } }
    return $valid | Sort-Object sequence -Descending | Select-Object -First 1
}
function Write-InterruptedState {
    # Model the durable descriptor left by a killed launcher, using the real
    # state envelope format. The next real launcher must perform recovery.
    $state = Read-State
    $body = [ordered]@{ sequence=([long]$state.sequence+1); confirmed=$baseline; previous=@{id='';inventoryHash=''}; candidate=$candidate; launching=$true } | ConvertTo-Json -Depth 8 -Compress
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = [BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes($body))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    $slot = ([long]$state.sequence+1) % 2
    Json-Write (Join-Path $install "portable-state-$slot.json") ([ordered]@{body=$body;sha256=$digest})
}
function Owned-Processes {
    @(Get-CimInstance Win32_Process -Filter "Name='LightHostModern.exe' OR Name='LightHostModernWinUI.exe'" | Where-Object {
        $_.ExecutablePath -and $_.ExecutablePath.StartsWith($install+'\',[StringComparison]::OrdinalIgnoreCase) -and
        $_.CommandLine -and $_.CommandLine.Contains('--test-profile='+$profileName)
    })
}
function Wait-Profile([string] $ExpectedPayload) {
    $deadline = [DateTime]::UtcNow.AddSeconds(35)
    do {
        try {
            $info = Get-Content -LiteralPath (Join-Path $testProfileDirectory 'profile.json') -Raw | ConvertFrom-Json
            $owned = @(Owned-Processes)
            $runningHost = $owned | Where-Object { $_.ProcessId -eq $info.pid -and $_.ExecutablePath -eq (Join-Path $ExpectedPayload 'LightHostModern.exe') }
            if ($runningHost) {
                $snapshot = Send-HostRequest $info.pipe 'snapshot'
                $runningUi = @($owned | Where-Object Name -eq 'LightHostModernWinUI.exe')
                if ($snapshot.hostPid -eq $info.pid -and $runningUi.Count -eq 1) {
                    $candidateUi = Get-Process -Id $runningUi[0].ProcessId
                    if ($candidateUi.MainWindowHandle -ne [IntPtr]::Zero) {
                        $script:hostProcess = Get-Process -Id $info.pid
                        $script:uiProcess = $candidateUi
                        $null = $script:hostProcess.Handle; $null = $script:uiProcess.Handle
                        $script:pipe = $info.pipe; $script:session = $snapshot.hostSession
                        Assert (!$snapshot.audioSelection.processingAvailable -and !$snapshot.audioSelection.driverAvailable) 'Isolated profile opened real audio'
                        Assert ($snapshot.appConfig.closeBehavior -eq 'quit') 'Persisted host preference did not survive restart'
                        return $snapshot
                    }
                }
            }
        } catch { $lastStartupError = $_.Exception.Message }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Isolated packaged host/UI startup timed out: $lastStartupError"
}
function Stop-Profile {
    if (!$script:hostProcess -or $script:hostProcess.HasExited) { return }
    Assert (@(Owned-Processes | Where-Object ProcessId -eq $script:hostProcess.Id).Count -eq 1) 'Host process is no longer owned by this fixture'
    Send-HostRequest $script:pipe 'quit-host' -Session $script:session | Out-Null
    Assert ($script:hostProcess.WaitForExit(20000)) 'Isolated host did not shut down normally'
    if ($script:uiProcess) { Assert ($script:uiProcess.WaitForExit(15000)) 'Isolated UI did not close with its host' }
}
function Assert-Preserved {
    Assert ((Hash $launcher) -eq $launcherHash) 'Stable launcher changed during update'
    Assert ((Hash (Join-Path $baselinePath 'LightHostModern.exe')) -eq $baselineHostHash) 'Previous host executable changed'
    Assert ([IO.File]::ReadAllText($markerPath) -eq $marker) 'Profile user data changed'
    [xml]$xml = [IO.File]::ReadAllText($settings)
    Assert (@($xml.PROPERTIES.VALUE | Where-Object { $_.name -eq 'portableUpdateSentinel' -and $_.val -eq $marker }).Count -eq 1) 'Host profile sentinel disappeared'
    Assert ([IO.File]::ReadAllText((Join-Path $testProfileDirectory 'ui-settings.ini')).Contains($marker)) 'UI profile sentinel disappeared'
    Assert ([IO.File]::ReadAllText((Join-Path $install 'user-settings.json')) -eq $marker) 'Portable root user data changed'
}
function Start-Apply {
    foreach ($event in @($script:arm,$script:cancel,$script:ready)) { if ($event) { $event.Dispose() } }
    $events = 'Local\LightHostModernUpdate-' + [guid]::NewGuid().ToString('N')
    $script:arm = [Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$events+'-arm')
    $script:cancel = [Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$events+'-cancel')
    $script:ready = [Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$events+'-ready')
    $arguments = @(Helper-Arguments 'apply') + @('--host-pid',[string]$hostProcess.Id,'--host-created',[string]($hostProcess.StartTime.ToFileTimeUtc()),
        '--ui-pid',[string]$uiProcess.Id,'--ui-created',[string]($uiProcess.StartTime.ToFileTimeUtc()),'--events',$events,
        '--test-profile',$profileName,'--profile-root',$profileRoot)
    $script:helperProcess = Start-Hidden $fixtureHelper $arguments
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    while (!$ready.WaitOne(100)) {
        if ($helperProcess.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw ('Helper did not prepare: ' + (Result-State)) }
    }
    Assert ((Result-State) -eq 'ready') 'Helper signalled ready without its durable ready result'
    Assert ((Read-State).candidate.id -eq $null -or (Read-State).candidate.id -eq '') 'Preparation activated candidate before handshake'
}
$productionFiles = @((Join-Path $env:APPDATA 'LightHostModern/LightHostModern.settings'), (Join-Path $env:LOCALAPPDATA 'LightHostModern/ui-settings.ini'))
$productionBefore = @{}
foreach ($file in $productionFiles) { $productionBefore[$file] = if (Test-Path -LiteralPath $file) { Hash $file } else { '' } }
try {
    $metadata = Get-Content -LiteralPath (Join-Path $packages 'release-artifacts.json') -Raw | ConvertFrom-Json
    $artifact = @($metadata.artifacts | Where-Object name -eq 'LightHostModern-Portable.zip')
    Assert ($artifact.Count -eq 1 -and $artifact[0].version -eq $ExpectedVersion) 'Expected portable release artifact is missing'
    $artifact = $artifact[0]
    foreach ($name in 'LightHostModern-Portable.zip','update-manifest.json','update-manifest.sig') { Copy-Item -LiteralPath (Join-Path $packages $name) -Destination (Join-Path $operation $name) }
    $package = Join-Path $operation 'LightHostModern-Portable.zip'
    $packageHash = Hash $package
    Assert (('sha256:'+$packageHash) -eq $artifact.digest -and (Get-Item -LiteralPath $package).Length -eq $artifact.size) 'Copied release package differs from metadata'
    $productionHelper = Join-Path (Get-TestBuildDirectory) 'LightHostModern_artefacts/Release/LightHostModernUpdateHelper.exe'
    $helperProcess = Start-Hidden $productionHelper (Helper-Arguments 'validate-signed')
    Assert ($helperProcess.WaitForExit(60000) -and $helperProcess.ExitCode -eq 0 -and (Result-State) -eq 'validated') 'Production helper rejected signed release package'
    Passed 'Production helper validates exact release ZIP and manifest with compiled trust roots'
    # The release helper must reject its own version even when correctly signed.
    $helperProcess = Start-Hidden $productionHelper (Helper-Arguments 'apply')
    Assert ($helperProcess.WaitForExit(60000) -and $helperProcess.ExitCode -ne 0 -and (Result-State) -eq 'downgrade_refused') 'Same-version automatic update was not refused'
    Passed 'Production helper refuses same-version update before touching any host'

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($package,$install)
    $layout = Get-Content -LiteralPath (Join-Path $install 'portable-layout.json') -Raw | ConvertFrom-Json
    $candidate = $layout.initial
    $candidatePath = Join-Path $install ('versions/'+$candidate.id)
    $baselineId = '1.9.9-' + [guid]::NewGuid().ToString('N')
    $baselinePath = Join-Path $install ('versions/'+$baselineId)
    # Both resolved move targets are newly extracted children of our fixture.
    foreach ($path in $candidatePath,$baselinePath) {
        Assert ([IO.Path]::GetFullPath($path).StartsWith($install+'\versions\',[StringComparison]::OrdinalIgnoreCase)) 'Baseline move escaped fixture versions directory'
        No-Reparse $path
    }
    [IO.Directory]::Move($candidatePath,$baselinePath)
    $inventoryPath = Join-Path $baselinePath 'payload-manifest.json'
    $inventory = Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
    $inventory.version = '1.9.9'
    Json-Write $inventoryPath $inventory
    $baseline = [pscustomobject]@{id=$baselineId;inventoryHash=(Hash $inventoryPath)}
    $layout.initial = $baseline
    Json-Write (Join-Path $install 'portable-layout.json') $layout
    $launcher = Join-Path $install 'LightHostModern.exe'
    $launcherHash = Hash $launcher
    $baselineHostHash = Hash (Join-Path $baselinePath 'LightHostModern.exe')
    [IO.File]::WriteAllText((Join-Path $install 'user-settings.json'),$marker,$utf8)
    $launcherProcess = Start-Hidden $launcher @('--show-ui',('--test-profile='+$profileName),('--profile-root='+$profileRoot))
    Assert ($launcherProcess.WaitForExit(25000) -and $launcherProcess.ExitCode -eq 0) 'Initial stable launcher failed'
    $null = Wait-Profile $baselinePath
    Passed 'Packaged launcher starts actual host and WinUI in a separate profile with audio suspended'

    Start-Apply
    Assert (!$hostProcess.HasExited -and !$uiProcess.HasExited) 'Preparation stopped application processes'
    $null = $cancel.Set()
    Assert ($helperProcess.WaitForExit(15000) -and (Result-State) -eq 'cancelled') 'Cancellation did not finish safely'
    Assert (!$hostProcess.HasExited -and !$uiProcess.HasExited -and !(Read-State).candidate.id) 'Cancelled preparation changed the active application'
    Assert-Preserved
    Passed 'Cancellation after authenticated prepare preserves running host, UI, profile and confirmed payload'

    Start-Apply
    $previousHostPid = $hostProcess.Id
    $null = $arm.Set()
    Start-Sleep -Milliseconds 400
    Assert (!$helperProcess.HasExited -and (Result-State) -eq 'ready' -and !(Read-State).candidate.id) 'Arming applied before process exits'
    Stop-Profile
    Assert ($helperProcess.WaitForExit(60000) -and $helperProcess.ExitCode -eq 0 -and (Result-State) -eq 'completed') 'Portable helper apply failed'
    $null = Wait-Profile $candidatePath
    Assert ($hostProcess.Id -ne $previousHostPid) 'Helper did not restart a new host process'
    $state = Read-State
    Assert ($state.confirmed.id -eq $candidate.id -and $state.previous.id -eq $baseline.id -and !$state.candidate.id -and !$state.launching) 'Actual launcher did not confirm candidate readiness'
    Assert-Preserved
    Passed 'Real helper arm plus host/UI exit activates release, restarts stable launcher and confirms candidate'
    Passed 'Host preference, host/UI sentinels and arbitrary profile/root user files survive the update'

    Stop-Profile
    Write-InterruptedState
    $launcherProcess = Start-Hidden $launcher @('--show-ui',('--test-profile='+$profileName),('--profile-root='+$profileRoot))
    Assert ($launcherProcess.WaitForExit(25000) -and $launcherProcess.ExitCode -eq 0) 'Recovery launcher failed'
    $null = Wait-Profile $baselinePath
    $state = Read-State
    Assert ($state.confirmed.id -eq $baseline.id -and !$state.candidate.id -and !$state.launching) 'Interrupted candidate was retried instead of falling back'
    Assert-Preserved
    Stop-Profile
    Passed 'Actual launcher recovers an interrupted candidate through previous complete payload and retains profile'
    Assert ((Hash $package) -eq $packageHash -and (Hash (Join-Path $packages 'LightHostModern-Portable.zip')) -eq $packageHash) 'Release artifact changed during integration'
} catch {
    $failure = $_.Exception.Message
    $results.Add([ordered]@{name='Portable update integration';status='failed';error=$failure})
} finally {
    if ($cancel) { $null = $cancel.Set() }
    if ($helperProcess -and !$helperProcess.HasExited -and !$helperProcess.WaitForExit(10000)) { $helperProcess.Kill(); $helperProcess.WaitForExit() }
    try { Stop-Profile } catch { Write-Warning ('Fixture shutdown: '+$_.Exception.Message) }
    # Last-resort cleanup is restricted to the unique fixture path AND profile.
    foreach ($owned in @(Owned-Processes)) {
        $p = Get-Process -Id $owned.ProcessId -ErrorAction SilentlyContinue
        if ($p -and !$p.HasExited) {
            $null = $p.Handle
            # Pin and re-check identity before last-resort termination, so a
            # PID reused after the CIM snapshot cannot affect another session.
            if ($p.MainModule.FileName -eq $owned.ExecutablePath -and
                [Math]::Abs(($p.StartTime.ToUniversalTime() - $owned.CreationDate.ToUniversalTime()).TotalMilliseconds) -lt 1) {
                $p.Kill(); $null = $p.WaitForExit(5000)
            }
        }
    }
    foreach ($event in @($arm,$cancel,$ready)) { if ($event) { $event.Dispose() } }
    foreach ($file in $productionFiles) {
        $after = if (Test-Path -LiteralPath $file) { Hash $file } else { '' }
        if ($after -ne $productionBefore[$file]) { $failure = 'Production preferences changed while isolated integration was running'; $results.Add([ordered]@{name='Production preference isolation';status='failed';error=$failure}) }
    }
    Json-Write (Join-Path $work 'result.json') ([ordered]@{
        passed=(!$failure); package=$packages; version=$ExpectedVersion; profile=$testProfileDirectory
        trust='compiled production trustedUpdateKeys; exact release manifest; no key export or trust override'
        baseline='Synthetic inventory 1.9.9 with unchanged current package binaries and test-only helper version gate'
        recovery='Interrupted-launch descriptor simulation; not VM power-loss certification'
        fixtureHelper=$fixtureHelper; fixtureHelperSha256=(Hash $fixtureHelper); scenarios=@($results.ToArray())
    })
}
$results | Format-Table -AutoSize
Write-Host "Portable update integration results: $work"
if ($failure) { throw $failure }
