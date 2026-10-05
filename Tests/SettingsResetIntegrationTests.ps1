param([string] $HostExecutable = "$PSScriptRoot\..\out\build\windows-vs2022\LightHostModern_artefacts\Release\LightHostModern.exe")
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\HostProtocol.ps1"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$root = Join-Path $repo 'out\test-profiles'
$name = 'settings-reset-' + [guid]::NewGuid().ToString('N')
$directory = Join-Path $root $name
$script:hostProcess = $null
function Start-ResetTestHost {
    $script:hostProcess = Start-Process -FilePath ([IO.Path]::GetFullPath($HostExecutable)) -ArgumentList @("--test-profile=$name", "--profile-root=`"$root`"") -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        try {
            $info = Get-Content -LiteralPath (Join-Path $directory 'profile.json') -Raw | ConvertFrom-Json
            if ($info.pid -eq $script:hostProcess.Id) {
                $script:pipe = $info.pipe
                $snapshot = Send-HostRequest $script:pipe 'snapshot'
                if ($snapshot.audioSelection.driverAvailable -or $snapshot.diagnostics.recoveryState -ne 'suspended') { throw 'Reset test opened audio.' }
                $script:session = $snapshot.hostSession
                return $snapshot
            }
        } catch { if ($_.Exception.Message -eq 'Reset test opened audio.') { throw } }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline -and !$script:hostProcess.HasExited)
    throw 'Isolated reset host did not start.'
}
function Stop-ResetTestHost {
    Send-HostRequest $script:pipe 'quit-host' -Session $script:session | Out-Null
    if (!$script:hostProcess.WaitForExit(15000)) { throw 'Isolated reset host did not close.' }
}
try {
    $null = Start-ResetTestHost
    foreach ($label in 'Profile before reset', 'Profile creating backup') {
        $before = Send-HostRequest $script:pipe 'snapshot'
        $request = @{ action='create'; name=$label; includeAudio=$false; revision=[string]$before.chainVersion;
            profileId=$before.operating.activeProfile; generation=[string]$before.operating.generation }
        $result = Wait-HostOperation $script:pipe (Send-HostRequest $script:pipe 'operating-command' @($request) -Session $script:session)
        if ($result.status -ne 'ok') { throw 'Could not create reset profile fixture.' }
    }
    Stop-ResetTestHost
    $preferences = Join-Path $directory 'LightHostModern.settings'
    if (!(Test-Path -LiteralPath ($preferences + '.profiles.xml.bak'))) { throw 'Profile backup fixture was not created.' }
    $export = Join-Path $directory 'My exported profile.xml'
    [IO.File]::WriteAllText($export, 'preserve exported document')
    $recovery = Join-Path $directory 'chain-edit-recovery-00000000000000000000000000000001.json'
    [IO.File]::WriteAllText($recovery, '{"old":"pending graph"}')
    foreach ($attempt in 1..2) {
        if ($attempt -eq 1) {
            [IO.File]::WriteAllText($preferences + '.factory-reset', 'test')
        } else {
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'Utilities/Reset Settings.ps1') -SettingsDirectory $directory
            if ($LASTEXITCODE -ne 0) { throw 'Reset helper could not schedule the repeated reset.' }
        }
        $after = Start-ResetTestHost
        if (!$after.operating.catalogWritable -or !$after.operating.writable -or $after.operating.profiles.Count -ne 2 -or
            @($after.operating.profiles | Where-Object { !$_.isDefault }).Count -ne 0) { throw 'Reset restored old profiles or made the catalogue read-only.' }
        if ((Test-Path -LiteralPath $recovery) -or (Test-Path -LiteralPath ($preferences + '.factory-reset'))) { throw 'Reset left pending edit or reset markers.' }
        if ([IO.File]::ReadAllText($export) -ne 'preserve exported document') { throw 'Reset modified a user export.' }
        Stop-ResetTestHost
    }
    [ordered]@{ status='passed'; directory=$directory; attempts=2; defaultsOnly=$true; writable=$true; exportsPreserved=$true; audioOpened=$false } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $directory 'settings-reset-result.json') -Encoding UTF8
    Write-Output "Settings reset integration passed: $directory"
} finally {
    if ($script:hostProcess -and !$script:hostProcess.HasExited) {
        try { Stop-ResetTestHost } catch { $script:hostProcess.Kill(); [void]$script:hostProcess.WaitForExit(5000) }
    }
}
