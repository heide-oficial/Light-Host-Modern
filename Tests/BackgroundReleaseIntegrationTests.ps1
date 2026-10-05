param([Parameter(Mandatory)][string]$HostExecutable,
      [string]$OutputDirectory='out/background-release-integration')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/HostProtocol.ps1"
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outRoot=Join-Path $repo 'out'
$output=[IO.Path]::GetFullPath($OutputDirectory)
if (!$output.StartsWith($outRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)) {
    throw 'Select a new output directory below the workspace out directory.'
}
$HostExecutable=(Resolve-Path -LiteralPath $HostExecutable).Path
if (!$HostExecutable.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase) -or
    [IO.Path]::GetFileName($HostExecutable) -ne 'LightHostModern.exe') { throw 'A workspace host build is required.' }
foreach ($target in @($output,$HostExecutable)) {
    for ($cursor=$target; $cursor; $cursor=[IO.Path]::GetDirectoryName($cursor)) {
        if ((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Test paths must not contain reparse points.'
        }
    }
}
New-Item -ItemType Directory -Path $output | Out-Null
$profileRoot=Join-Path $output 'profiles'
$name='background-release-'+[guid]::NewGuid().ToString('N')
$profile=Join-Path $profileRoot $name
$ini=Join-Path $profile 'ui-settings.ini'
$fixture=Join-Path $profile 'Temp/background-release-fixture.json'
$captures=Join-Path $profile 'Logs/Captures'
New-Item -ItemType Directory -Path (Split-Path $fixture -Parent),$captures | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $captures 'state.txt'),"armed`n`n`n`n",$utf8)
$results=[Collections.Generic.List[object]]::new()
$process=$null; $pipe=''; $session=''; $birth=$null
if (!('BackgroundReleaseTestIni' -as [type])) {
    Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
using System.Text;
public static class BackgroundReleaseTestIni {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
    public static extern bool WritePrivateProfileString(string section,string key,string value,string file);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
    public static extern uint GetPrivateProfileString(string section,string key,string fallback,StringBuilder value,uint size,string file);
}
'@
}
function Set-Preference([bool]$Enabled) {
    if (![BackgroundReleaseTestIni]::WritePrivateProfileString('Updates','NotifyReleasesOnWindows',$(if ($Enabled) {'1'} else {'0'}),$ini)) {
        throw 'Could not write the isolated notification preference.'
    }
}
function Last-Notification {
    $value=[Text.StringBuilder]::new(128)
    $null=[BackgroundReleaseTestIni]::GetPrivateProfileString('Updates','LastWindowsNotifiedRelease','',$value,128,$ini)
    $value.ToString()
}
function Set-Release([string]$Version) {
    $body=@{tag_name=$Version;html_url="https://github.com/heide-oficial/Light-Host-Modern/releases/tag/$Version";draft=$false;prerelease=$false}
    [IO.File]::WriteAllText($fixture,($body|ConvertTo-Json -Compress),$utf8)
}
function Read-Capture {
    $text=[Text.StringBuilder]::new()
    foreach ($file in @(Get-ChildItem -LiteralPath $captures -Filter '*.log' -File -Recurse)) {
        $stream=[IO.File]::Open($file.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        try {
            if ($stream.Length -gt 8MB) { throw 'Unexpectedly large isolated capture.' }
            $reader=[IO.StreamReader]::new($stream)
            try { $null=$text.AppendLine($reader.ReadToEnd()) } finally { $reader.Dispose() }
        } finally { $stream.Dispose() }
    }
    $text.ToString()
}
function Notification-Count { ([regex]::Matches((Read-Capture),'Windows release notification submitted:')).Count }
function Assert-NoUi {
    $matching=@(Get-CimInstance Win32_Process -Filter "Name='LightHostModernWinUI.exe'" | Where-Object {
        $_.CommandLine -and $_.CommandLine.Contains("--test-profile=$name")
    })
    if ($matching.Count) { throw 'The background check launched a UI for the isolated profile.' }
}
function Wait-For([scriptblock]$Condition,[string]$Failure,[int]$TimeoutMs=15000) {
    $deadline=[DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while (!(& $Condition)) {
        if (($process -and $process.HasExited) -or [DateTime]::UtcNow -ge $deadline) { throw $Failure }
        Start-Sleep -Milliseconds 50
    }
}
function Start-Host {
    $script:pipe=''; $script:session=''
    $script:process=Start-Process -FilePath $HostExecutable -ArgumentList @("--test-profile=$name",('--profile-root="'+$profileRoot+'"'),'--no-audio') -WindowStyle Hidden -PassThru
    $null=$process.Handle; $script:birth=$process.StartTime.ToUniversalTime()
    $metadata=Join-Path $profile 'profile.json'
    Wait-For {
        try {
            $info=Get-Content -LiteralPath $metadata -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($info.pid -eq $process.Id -and $info.pipe) { $script:pipe=$info.pipe; return $true }
        } catch { }
        return $false
    } 'Host did not become ready.'
    $script:session=(Send-HostRequest $pipe 'hello').hostSession
    $snapshot=Send-HostRequest $pipe 'snapshot'
    if ($snapshot.audioSelection.processingAvailable) { throw 'Isolated test opened audio.' }
    Assert-NoUi
}
function Mutate([string]$Command,[object[]]$Arguments=@()) {
    $result=Wait-HostOperation $pipe (Send-HostRequest $pipe $Command $Arguments -Session $session)
    if ($result.status -ne 'ok') { throw ($result | ConvertTo-Json -Depth 8 -Compress) }
}
function Stop-Host {
    if (!$process -or $process.HasExited) { return }
    $clock=[Diagnostics.Stopwatch]::StartNew()
    # Receipt proves admission; process exit proves completion. Polling the
    # operation afterward races successful shutdown and reconnects to a dead pipe.
    $accepted=Send-HostRequest $pipe 'quit-host' -Session $session
    if ($accepted.status -notin @('ok','operation')) { throw ($accepted | ConvertTo-Json -Depth 8 -Compress) }
    if (!$process.WaitForExit(5000)) { throw 'Background checker prevented bounded host shutdown.' }
    $clock.Stop()
    if ($process.ExitCode -ne 0) { throw "Host exited with code $($process.ExitCode)." }
    if ($clock.ElapsedMilliseconds -gt 6000) { throw 'Host shutdown exceeded the test budget.' }
    Assert-NoUi
}
function Scenario([string]$Title,[scriptblock]$Work) {
    $clock=[Diagnostics.Stopwatch]::StartNew(); & $Work; $clock.Stop()
    $results.Add(@{name=$Title;status='passed';milliseconds=$clock.ElapsedMilliseconds})
    Write-Output "PASS $Title"
}
try {
    Scenario 'Startup without GUI checks a local release and submits a test-only Windows notice' {
        Set-Preference $true; Set-Release 'v99.0.0'; Start-Host
        Wait-For { (Last-Notification) -match '^v?99\.0\.0$' -and (Notification-Count) -eq 1 } 'Background startup notice was not recorded.'
        Assert-NoUi
    }
    Scenario 'UI IPC and host checker share normalized per-run notification deduplication' {
        Mutate 'notify-release' @('99.0.0'); Mutate 'notify-release' @('v99.0.0')
        Start-Sleep -Milliseconds 300
        if ((Notification-Count) -ne 1) { throw 'The same release was submitted twice in one host run.' }
        Assert-NoUi
    }
    Scenario 'Restart can announce the same release without deleting its persisted last-notified value' {
        Stop-Host; Start-Host
        Wait-For { (Notification-Count) -eq 2 } 'Restart incorrectly suppressed the previously announced release.'
        Assert-NoUi
    }
    Scenario 'Disabled preference suppresses the notice and enabling it live triggers a check' {
        Stop-Host; Set-Preference $false; Set-Release 'v99.0.1'; Start-Host
        Start-Sleep -Milliseconds 3000
        if ((Notification-Count) -ne 2 -or (Last-Notification) -match '^v?99\.0\.1$') { throw 'Windows notification preference was ignored.' }
        Set-Preference $true
        Wait-For { (Last-Notification) -match '^v?99\.0\.1$' -and (Notification-Count) -eq 3 } 'Live enable did not discover the release.'
        Stop-Host
    }
    Scenario 'No newer release leaves the host responsive without a notification or GUI' {
        Set-Release 'v0.0.1'; Start-Host
        Start-Sleep -Milliseconds 3000
        if ((Notification-Count) -ne 3) { throw 'An older release produced a notification.' }
        Stop-Host
    }
    Scenario 'Test profile without an explicit fixture stays offline and closes normally' {
        # The exact fixture is owned by this new output directory; preserve it as evidence.
        Move-Item -LiteralPath $fixture -Destination ($fixture+'.used')
        Start-Host
        Start-Sleep -Milliseconds 3000
        if ((Notification-Count) -ne 3) { throw 'A test profile without a fixture produced a release notice.' }
        Stop-Host
    }
    @{passed=$true;profile=$profile;host=$HostExecutable;realAudioOpened=$false;realToastSent=$false;externalNetworkUsed=$false;scenarios=$results} |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'results.json') -Encoding UTF8
} catch {
    @{passed=$false;error=$_.Exception.Message;scriptStack=$_.ScriptStackTrace;profile=$profile;scenarios=$results} |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'results.json') -Encoding UTF8
    throw
} finally {
    if ($process -and !$process.HasExited) {
        try { Stop-Host } catch { Write-Warning $_.Exception.Message }
        if (!$process.HasExited) {
            $identity=Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
            if ($identity -and $identity.ExecutablePath -eq $HostExecutable -and
                $identity.CommandLine.Contains("--test-profile=$name") -and
                $process.StartTime.ToUniversalTime() -eq $birth) {
                $process.Kill(); $null=$process.WaitForExit(5000)
            } else { Write-Warning 'Process identity changed; cleanup refused to terminate it.' }
        }
    }
    # A failure that unexpectedly created UI must not leave that owned fixture running.
    foreach ($identity in @(Get-CimInstance Win32_Process -Filter "Name='LightHostModernWinUI.exe'" | Where-Object {
        $_.CommandLine -and $_.CommandLine.Contains("--test-profile=$name") -and
        $_.ExecutablePath -and $_.ExecutablePath.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)
    })) {
        $owned=Get-Process -Id $identity.ProcessId -ErrorAction SilentlyContinue
        if ($owned -and !$owned.HasExited -and $owned.MainModule.FileName -eq $identity.ExecutablePath -and
            [Math]::Abs(($owned.StartTime.ToUniversalTime()-$identity.CreationDate.ToUniversalTime()).TotalMilliseconds) -lt 1) {
            $owned.Kill(); $null=$owned.WaitForExit(5000)
        }
    }
}
