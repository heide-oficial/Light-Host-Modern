param([string] $HostExecutable = "$PSScriptRoot\..\out\build\windows-vs2022\LightHostModern_artefacts\Release\LightHostModern.exe")
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\HostProtocol.ps1"
$repo = (Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$root = Join-Path $repo 'out\test-profiles'
$name = 'state-' + [guid]::NewGuid().ToString('N')
$directory = Join-Path $root $name
New-Item -ItemType Directory -Path $directory -Force | Out-Null
& "$(Get-TestBuildDirectory)\Release\LightHostModernPluginInstanceTests.exe" --write-legacy-fixture (Join-Path $directory 'LightHostModern.settings')
if ($LASTEXITCODE -ne 0) { throw 'Fixture failed.' }
$process = $null
$eventPipe = $null
function Start-EventRead($manifest) {
    $script:eventPipe = [IO.Pipes.NamedPipeClientStream]::new('.', ($info.pipe + '-events').Replace('\\.\pipe\', ''), [IO.Pipes.PipeDirection]::InOut, [IO.Pipes.PipeOptions]::Asynchronous)
    $eventPipe.Connect(5000)
    $eventPipe.ReadMode = [IO.Pipes.PipeTransmissionMode]::Message
    $wire = @{version=5; id='event-read'; command='events'; args=@(@{hostSession=$manifest.hostSession; afterSequence=$manifest.eventSequence; waitMs=4000})} | ConvertTo-Json -Depth 8 -Compress
    $bytes = [Text.Encoding]::UTF8.GetBytes($wire)
    $eventPipe.Write($bytes, 0, $bytes.Length)
    $script:buffer = [byte[]]::new(65536)
    $script:read = $eventPipe.ReadAsync($buffer, 0, $buffer.Length)
}
try {
    $process = Start-Process -FilePath $HostExecutable -ArgumentList @("--test-profile=$name", ('--profile-root="'+$root+'"')) -WindowStyle Hidden -PassThru
    $metadata = Join-Path $directory 'profile.json'
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (!(Test-Path -LiteralPath $metadata)) {
        if ($process.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw 'Host did not start.' }
        Start-Sleep -Milliseconds 100
    }
    $info = Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
    $manifest = Send-HostRequest -PipeName $info.pipe -Command 'snapshot-manifest'
    if (!$manifest.snapshotId -or $manifest.collections.activePlugins -ne 2 -or $manifest.activePlugins) { throw 'Invalid manifest.' }
    $page = Send-HostRequest -PipeName $info.pipe -Command 'snapshot-page' -Arguments @(@{snapshotId=$manifest.snapshotId; collection='activePlugins'; offset=0; limit=1})
    if ($page.items.Count -ne 1 -or $page.items[0].bypassed) { throw 'Invalid first page.' }
    Start-EventRead $manifest
    Start-Sleep -Milliseconds 100
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $accepted = Send-HostRequest -PipeName $info.pipe -Command 'toggle-bypass' -Arguments @($page.items[0].instanceId) -Session $manifest.hostSession
    $result = Wait-HostOperation -PipeName $info.pipe -Accepted $accepted
    $clock.Stop()
    if ($result.status -ne 'ok' -or $clock.ElapsedMilliseconds -gt 2000) { throw 'Event wait blocked the command transport.' }
    if (!$read.Wait(5000) -or $read.Result -le 0 -or !$eventPipe.IsMessageComplete) { throw 'Event response incomplete.' }
    $event = [Text.Encoding]::UTF8.GetString($buffer, 0, $read.Result) | ConvertFrom-Json
    $receipt = [Text.Encoding]::UTF8.GetBytes('received'); $eventPipe.Write($receipt, 0, $receipt.Length)
    $eventPipe.Dispose(); $eventPipe = $null
    # Session-save/startup diagnostics can legitimately publish a wildcard chain
    # event before the bypass mutation. Consume ordered events until that exact
    # instance delta arrives; do not mistake unrelated activity for a lost delta.
    $eventDeadline = [DateTime]::UtcNow.AddSeconds(5)
    while ($event.hostSession -eq $manifest.hostSession -and $event.changes.chain -notcontains $page.items[0].instanceId -and [DateTime]::UtcNow -lt $eventDeadline) {
        $event = Send-HostRequest -PipeName ($info.pipe + '-events') -Command 'events' -Arguments @(@{hostSession=$manifest.hostSession; afterSequence=$event.sequence; waitMs=1000})
    }
    if ($event.hostSession -ne $manifest.hostSession -or $event.sequence -le $manifest.eventSequence -or $event.changes.chain -notcontains $page.items[0].instanceId) { throw ('Chain event omitted the changed UUID: ' + ($event | ConvertTo-Json -Depth 10 -Compress)) }
    $oldPage = Send-HostRequest -PipeName $info.pipe -Command 'snapshot-page' -Arguments @(@{snapshotId=$manifest.snapshotId; collection='activePlugins'; offset=0; limit=100})
    if ($oldPage.items[0].bypassed) { throw 'An in-progress snapshot changed after a mutation.' }
    $latest = Send-HostRequest -PipeName $info.pipe -Command 'snapshot-manifest'
    $newPage = Send-HostRequest -PipeName $info.pipe -Command 'snapshot-page' -Arguments @(@{snapshotId=$latest.snapshotId; collection='activePlugins'; offset=0; limit=100})
    if (!$newPage.items[0].bypassed -or $latest.eventSequence -lt $event.sequence) { throw 'New snapshot lost the event revision.' }
    $wrong = Send-HostRequest -PipeName ($info.pipe + '-events') -Command 'events' -Arguments @(@{hostSession='old-host'; afterSequence=0; waitMs=0})
    if ($wrong.error.code -ne 'host_restarted') { throw 'Old event session accepted.' }
    $future = Send-HostRequest -PipeName ($info.pipe + '-events') -Command 'events' -Arguments @(@{hostSession=$latest.hostSession; afterSequence=999999; waitMs=0})
    if (!$future.resyncRequired) { throw 'Future cursor did not require a snapshot.' }
    Start-EventRead $latest
    Send-HostRequest -PipeName $info.pipe -Command 'quit-host' -Session $manifest.hostSession | Out-Null
    if (!$process.WaitForExit(3000)) { throw 'Pending event read prevented shutdown.' }
    @{passed=$true; profile=$directory; commandDuringEventWaitMs=$clock.ElapsedMilliseconds; eventSequence=$event.sequence; realAudioOpened=$false} | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $directory 'state-result.json') -Encoding UTF8
    Write-Output "PASS separate event transport, immutable pages, UUID deltas, sessions and pending-read shutdown: $directory"
} finally {
    if ($eventPipe) { $eventPipe.Dispose() }
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
}
