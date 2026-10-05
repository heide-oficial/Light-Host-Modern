# Prefer the locally provisioned UI CLI without changing the machine's PATH.
$testWinAppBin=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../out/tools/winapp-0.6.0/bin'))
if(!(Get-Command winapp -ErrorAction SilentlyContinue) -and (Test-Path -LiteralPath (Join-Path $testWinAppBin 'winapp.exe'))){$env:PATH=$testWinAppBin+';'+$env:PATH}
# Optional override ensures integration suites exercise the selected build.
function Get-TestBuildDirectory {
    if ($env:LIGHTHOST_TEST_BUILD_DIR) { return [IO.Path]::GetFullPath($env:LIGHTHOST_TEST_BUILD_DIR) }
    return [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\out\build\windows-vs2022'))
}

function Start-TestUi {
    param([Parameter(Mandatory)][string]$Directory, [Parameter(Mandatory)][string[]]$Arguments)
    if (!@($Arguments | Where-Object { $_.StartsWith('--test-profile=') }).Count) { throw 'UI tests require an isolated profile.' }
    # Exercise the same self-contained executable as the portable/host launcher.
    # Do not register a development MSIX or depend on winapp run deployment.
    $quoted = @($Arguments | ForEach-Object { '"' + $_.Replace('"','\"') + '"' })
    $process = Start-Process -FilePath (Join-Path $Directory 'LightHostModernWinUI.exe') -ArgumentList $quoted -WindowStyle Hidden -PassThru
    $global:LASTEXITCODE = 0
    return [pscustomobject]@{ ProcessId=$process.Id }
}

function Send-HostRequest {
    param([string] $PipeName, [string] $Command, [array] $Arguments = @(),
          [string] $Session = '', [string] $RequestId = [guid]::NewGuid().ToString('N'), [int] $TimeoutMs = 5000,
          [ValidateSet(3,4,5)][int] $ProtocolVersion = 5)
    Write-Verbose "IPC $Command ($RequestId)"
    $pipe = [IO.Pipes.NamedPipeClientStream]::new('.', $PipeName.Replace('\\.\pipe\', ''), [IO.Pipes.PipeDirection]::InOut, [IO.Pipes.PipeOptions]::Asynchronous)
    try {
        $pipe.Connect($TimeoutMs)
        $pipe.ReadMode = [IO.Pipes.PipeTransmissionMode]::Message
        $wire = @{ version = $ProtocolVersion; id = $RequestId; hostSession = $Session; command = $Command; args = @($Arguments) } | ConvertTo-Json -Depth 12 -Compress
        $bytes = [Text.Encoding]::UTF8.GetBytes($wire)
        $write = $pipe.WriteAsync($bytes, 0, $bytes.Length)
        if (!$write.Wait($TimeoutMs)) { throw 'Request write timed out; query the operation ID before retrying.' }
        $stream = [IO.MemoryStream]::new()
        try {
            $buffer = [byte[]]::new(16384)
            do {
                $read = $pipe.ReadAsync($buffer, 0, $buffer.Length)
                if (!$read.Wait($TimeoutMs)) { throw 'Response timed out; query the operation ID before retrying.' }
                $count = $read.Result
                if ($count -eq 0) { throw 'Host disconnected before completing the response.' }
                $stream.Write($buffer, 0, $count)
                if ($stream.Length -gt 4MB) { throw 'Response exceeded 4 MiB.' }
            } while (!$pipe.IsMessageComplete)
            $response = [Text.Encoding]::UTF8.GetString($stream.ToArray()) | ConvertFrom-Json
            $receipt = [Text.Encoding]::UTF8.GetBytes('received')
            if (!$pipe.WriteAsync($receipt, 0, $receipt.Length).Wait($TimeoutMs)) { throw 'Receipt timed out.' }
            if ($response.version -ne $ProtocolVersion -or $response.id -ne $RequestId) { throw 'Host response protocol mismatch.' }
            return $response
        } finally { $stream.Dispose() }
    } finally { $pipe.Dispose() }
}

function Wait-HostOperation {
    param([string] $PipeName, $Accepted, [int] $TimeoutMs = 10000)
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    $status = $Accepted
    while ($status.status -eq 'operation') {
        if ($status.operationState -in @('completed', 'failed', 'cancelled')) { return $status.result }
        if ([DateTime]::UtcNow -ge $deadline) { throw "Operation $($status.operationId) is still pending; it was not resubmitted." }
        Start-Sleep -Milliseconds 25
        $status = Send-HostRequest -PipeName $PipeName -Command 'operation-status' -Arguments @($Accepted.operationId, $Accepted.hostSession)
    }
    return $status
}
