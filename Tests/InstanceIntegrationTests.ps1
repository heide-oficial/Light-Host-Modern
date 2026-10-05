param(
    [string] $HostExecutable = "$PSScriptRoot\..\out\build\windows-vs2022\LightHostModern_artefacts\Release\LightHostModern.exe",
    [string] $FixtureWriter = "$PSScriptRoot\..\out\build\windows-vs2022\Release\LightHostModernPluginInstanceTests.exe"
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\HostProtocol.ps1"
$repo = (Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$root = Join-Path $repo 'out\test-profiles'
$name = 'instances-' + [guid]::NewGuid().ToString('N')
$directory = Join-Path $root $name
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$settings = Join-Path $directory 'LightHostModern.settings'
& $FixtureWriter --write-legacy-fixture $settings
if ($LASTEXITCODE -ne 0) { throw 'Could not create legacy fixture.' }
$originalHash = (Get-FileHash -LiteralPath $settings).Hash
$script:hostProcess = $null
function Start-TestHost {
    $script:hostProcess = Start-Process -FilePath $HostExecutable -ArgumentList @("--test-profile=$name", ('--profile-root="' + $root + '"')) -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $deadline) {
        $metadata = Join-Path $directory 'profile.json'
        if (Test-Path -LiteralPath $metadata) {
            try {
                $info = Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
                if ($info.pid -eq $script:hostProcess.Id) { return $info }
            } catch { }
        }
        if ($script:hostProcess.HasExited) { throw 'Host exited during instance migration.' }
        Start-Sleep -Milliseconds 50
    }
    throw 'Test host did not start.'
}
function Invoke-Mutation([string] $Command, [object[]] $Arguments = @()) {
    $accepted = Send-HostRequest -PipeName $info.pipe -Command $Command -Arguments $Arguments -Session $session
    $result = Wait-HostOperation -PipeName $info.pipe -Accepted $accepted
    if ($result.status -ne 'ok') { throw "Mutation failed: $Command" }
    return $result
}
try {
    $info = Start-TestHost
    $snapshot = Send-HostRequest -PipeName $info.pipe -Command 'snapshot'
    $session = $snapshot.hostSession
    $first = '22222222222222222222222222222222'
    $second = '11111111111111111111111111111111'
    if ($snapshot.activePlugins.Count -ne 2 -or $snapshot.activePlugins[0].instanceId -ne $first -or $snapshot.activePlugins[1].instanceId -ne $second) { throw 'Migration collapsed or reordered duplicates.' }
    if ($snapshot.diagnostics.sampleRate -ne $null -or $snapshot.activePlugins[0].loading -ne 'missing') { throw 'Missing fixture was not preserved without audio.' }
    if ($snapshot.diagnostics.inputLatency -ne $null -or $snapshot.diagnostics.outputLatency -ne $null) { throw 'Unavailable driver values must be null.' }
    Invoke-Mutation 'operating-command' @(@{action='card-color';id=$first;color='#801234AB'}) | Out-Null
    $colored = Send-HostRequest $info.pipe 'snapshot'
    if ($colored.activePlugins[0].cardColor -ne '#801234AB' -or $colored.activePlugins[1].cardColor -ne '') { throw 'Card color changed the wrong instance.' }
    Invoke-Mutation 'operating-command' @(@{action='create';name='Color persistence';includeAudio=$false}) | Out-Null
    $createdState = Send-HostRequest $info.pipe 'operating-state'
    $createdProfile = $createdState.profiles | Where-Object id -eq $createdState.activeProfile
    if (!$createdProfile -or $createdProfile.hasUpdates) { throw 'New profile must show Created.' }
    Start-Sleep -Milliseconds 30
    Invoke-Mutation 'operating-command' @(@{action='overwrite';id=$createdProfile.id}) | Out-Null
    $updatedState = Send-HostRequest $info.pipe 'operating-state'
    if (!(($updatedState.profiles | Where-Object id -eq $createdProfile.id).hasUpdates)) { throw 'Overwritten profile must show Last updated.' }
    $unicodeName = 'Voz: ' + [char]0x65e5 + [char]0x672c + [char]0x8a9e
    Invoke-Mutation 'rename-plugin' @($first, ('  ' + $unicodeName + '  ')) | Out-Null
    $renamed = Send-HostRequest -PipeName $info.pipe -Command 'snapshot'
    if ($renamed.activePlugins[0].name -ne $unicodeName -or $renamed.activePlugins[1].customName -ne '') { throw 'Unicode rename changed the wrong UUID or failed trimming.' }
    $details = Send-HostRequest -PipeName $info.pipe -Command 'instance-details' -Arguments @($first)
    if ($details.instanceId -ne $first -or $details.name -ne $snapshot.activePlugins[0].originalName -or $details.customName -ne $unicodeName) { throw 'Original identity and custom name were mixed.' }
    Invoke-Mutation 'rename-plugin' @($first, '') | Out-Null
    $invalid = Send-HostRequest -PipeName $info.pipe -Command 'rename-plugin' -Arguments @($first, "line`nbreak") -Session $session
    $invalidResult = Wait-HostOperation -PipeName $info.pipe -Accepted $invalid
    if ($invalidResult.error.code -ne 'invalid_instance_name') { throw 'Multiline rename was not rejected with a structured error.' }
    Invoke-Mutation 'swap-plugin-with' @($first, $second) | Out-Null
    $swapped = Send-HostRequest -PipeName $info.pipe -Command 'snapshot'
    if ($swapped.activePlugins[0].instanceId -ne $second) { throw 'UUID swap did not use actual order.' }
    Invoke-Mutation 'swap-plugin-with' @($first, $second) | Out-Null
    Invoke-Mutation 'reset-clipping' @(@{direction='all'}) | Out-Null
    Invoke-Mutation 'reset-clipping' @(@{direction='input';channel=0}) | Out-Null
    $meters = Send-HostRequest -PipeName $info.pipe -Command 'telemetry'
    if ($meters.diagnostics.meters.input.aggregate.clipped -or $meters.diagnostics.meters.output.aggregate.clipped -or $meters.diagnostics.processedBlocks -ne 0) { throw 'Suspended profile changed meters or processed audio.' }
    Invoke-Mutation 'toggle-bypass' @($first) | Out-Null
    Invoke-Mutation 'duplicate-plugin' @($first) | Out-Null
    $snapshot = Send-HostRequest -PipeName $info.pipe -Command 'snapshot'
    $third = $snapshot.activePlugins[1].instanceId
    if ($snapshot.activePlugins.Count -ne 3 -or $third -in @($first, $second) -or !$snapshot.activePlugins[1].bypassed) { throw 'Duplicate did not get independent identity and bypass.' }
    Invoke-Mutation 'move-plugin-to' @($third, $second) | Out-Null
    Invoke-Mutation 'remove-plugin' @($first) | Out-Null
    Send-HostRequest -PipeName $info.pipe -Command 'quit-host' -Session $session | Out-Null
    if (!$script:hostProcess.WaitForExit(10000)) { throw 'Host did not finish instance save.' }
    if ((Get-FileHash -LiteralPath ($settings + '.pre-session.bak')).Hash -ne $originalHash) { throw 'Legacy recovery copy changed.' }
    $saved = Get-Content -LiteralPath ($settings + '.session.json') -Raw | ConvertFrom-Json
    [xml] $sessionXml = $saved.sessionXml
    $records = $sessionXml.LIGHTHOSTSESSION.INSTANCE
    if ($records.Count -ne 2 -or $records[0].id -ne $second -or $records[1].id -ne $third -or $records[0].STATE -eq $records[1].STATE) { throw 'Distinct states or ordered records lost during save.' }
    $info = Start-TestHost
    $snapshot = Send-HostRequest -PipeName $info.pipe -Command 'snapshot'
    if ($snapshot.hostSession -eq $session -or $snapshot.activePlugins.Count -ne 2 -or $snapshot.activePlugins[1].instanceId -ne $third) { throw 'Restart repeated migration or changed UUIDs.' }
    if ($snapshot.globalMuted -or $snapshot.globalBypassed) { throw 'Runtime global controls leaked into persistence.' }
    if ($snapshot.activePlugins[1].cardColor -ne '#801234AB') { throw 'Duplicated card color did not survive restart.' }
    $session = $snapshot.hostSession
    Send-HostRequest -PipeName $info.pipe -Command 'quit-host' -Session $session | Out-Null
    if (!$script:hostProcess.WaitForExit(10000)) { throw 'Restarted host did not shut down.' }
    @{ passed = $true; profile = $directory; realAudioOpened = $false; instances = @($second, $third) } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $directory 'instance-result.json') -Encoding UTF8
    Write-Output "PASS: migration, missing plugins, UUID actions, distinct states and restart in $directory"
} finally {
    if ($script:hostProcess -and !$script:hostProcess.HasExited) { $script:hostProcess.Kill(); $script:hostProcess.WaitForExit() }
}
