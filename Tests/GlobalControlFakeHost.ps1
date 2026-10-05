param([string]$PipeName = 'LightHostModern-global-ui-test')
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path $PSScriptRoot '../out/global-ui-test'
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null
# These are fixture-owned markers, not host session or audio preferences.
foreach ($marker in @('stop', 'ready')) {
    $markerPath = Join-Path $testRoot $marker
    if (Test-Path -LiteralPath $markerPath) { Remove-Item -LiteralPath $markerPath }
}
$muted = $false
$bypassed = $false
$version = 1
while (!(Test-Path -LiteralPath (Join-Path $testRoot 'stop'))) {
    $pipe = [System.IO.Pipes.NamedPipeServerStream]::new($PipeName, [System.IO.Pipes.PipeDirection]::InOut, 1,
        [System.IO.Pipes.PipeTransmissionMode]::Message, [System.IO.Pipes.PipeOptions]::Asynchronous)
    try {
        Set-Content -LiteralPath (Join-Path $testRoot 'ready') -Value 'ready'
        $connection = $pipe.WaitForConnectionAsync()
        while (!$connection.Wait(500)) {
            if (Test-Path -LiteralPath (Join-Path $testRoot 'stop')) { return }
        }
        $stream = [System.IO.MemoryStream]::new()
        $buffer = [byte[]]::new(4096)
        do {
            $count = $pipe.Read($buffer, 0, $buffer.Length)
            if ($count -eq 0) { break }
            $stream.Write($buffer, 0, $count)
        } while (!$pipe.IsMessageComplete)
        $request = [Text.Encoding]::UTF8.GetString($stream.ToArray()) | ConvertFrom-Json
        $stream.Dispose()
        if ($request.command -eq 'set-global-mute') { $muted = [bool]$request.args[0]; $version++ }
        if ($request.command -eq 'set-global-bypass') { $bypassed = [bool]$request.args[0]; $version++ }
        $response = @{
            version = 5; hostSession = 'global-ui-fixture'; id = $request.id; status = 'online';
            globalMuted = $muted; globalBypassed = $bypassed; chainVersion = $version;
            pluginDbVersion = 1; audioConfigVersion = 1; knownPlugins = 0; activePluginCount = 0;
            activePlugins = @(); knownPluginList = @(); backend = 'None'; deviceName = 'None';
            inputLevel = 0; outputLevel = 0; sampleRate = 48000; bufferSize = 256;
            inputChannels = 2; outputChannels = 2
        }
        if ($request.command -like 'set-*') { $response.status = 'ok' }
        $payload = [Text.Encoding]::UTF8.GetBytes(($response | ConvertTo-Json -Depth 8 -Compress))
        $pipe.Write($payload, 0, $payload.Length)
        $null = $pipe.Read($buffer, 0, $buffer.Length)
        Add-Content -LiteralPath (Join-Path $testRoot 'requests.log') -Value "$($request.command) mute=$muted bypass=$bypassed"
    } catch {
        Add-Content -LiteralPath (Join-Path $testRoot 'errors.log') -Value $_
    } finally { $pipe.Dispose() }
}
