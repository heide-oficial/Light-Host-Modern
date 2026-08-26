param(
    [Parameter(Mandatory)]
    [string] $HostExecutable
)

$ErrorActionPreference = 'Stop'
$runKeyPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runValueName = 'Light Host Modern'
$resolvedHost = (Resolve-Path -LiteralPath $HostExecutable).Path
$originalExists = $false
$originalValue = $null
$originalKind = [Microsoft.Win32.RegistryValueKind]::String
$hostProcess = $null

function Send-HostCommand([int] $HostPid, [string] $Command) {
    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
        '.',
        "LightHost-$HostPid",
        [System.IO.Pipes.PipeDirection]::InOut
    )

    try {
        $pipe.Connect(10000)
        $writer = [System.IO.StreamWriter]::new($pipe, [System.Text.UTF8Encoding]::new($false), 1024, $true)
        $reader = [System.IO.StreamReader]::new($pipe, [System.Text.Encoding]::UTF8, $false, 1024, $true)
        try {
            $writer.Write($Command)
            $writer.Flush()
            return $reader.ReadToEnd()
        } finally {
            try { $reader.Dispose() } catch {}
            try { $writer.Dispose() } catch {}
        }
    } finally {
        try { $pipe.Dispose() } catch {}
    }
}

try {
    $runKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey(
        'Software\Microsoft\Windows\CurrentVersion\Run',
        $false
    )
    if ($null -ne $runKey -and $runKey.GetValueNames() -contains $runValueName) {
        $originalExists = $true
        $originalValue = $runKey.GetValue($runValueName, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        $originalKind = $runKey.GetValueKind($runValueName)
    }
    if ($null -ne $runKey) { $runKey.Dispose() }

    $hostProcess = Start-Process -FilePath $resolvedHost -ArgumentList '--startup' -PassThru
    $enableResponse = Send-HostCommand -HostPid $hostProcess.Id -Command 'set-start-with-windows:1'
    if ($enableResponse -notmatch '"status":"ok"') {
        throw "Host rejected startup enable command: $enableResponse"
    }

    $actualCommand = Get-ItemPropertyValue -LiteralPath $runKeyPath -Name $runValueName
    $expectedCommand = '"' + $resolvedHost + '" --startup'
    if ($actualCommand -cne $expectedCommand) {
        throw "Unexpected startup command. Expected '$expectedCommand', received '$actualCommand'."
    }
    Write-Output 'PASS: startup command uses the current host path and --startup mode'

    $disableResponse = Send-HostCommand -HostPid $hostProcess.Id -Command 'set-start-with-windows:0'
    if ($disableResponse -notmatch '"status":"ok"') {
        throw "Host rejected startup disable command: $disableResponse"
    }

    $remainingValue = Get-ItemProperty -LiteralPath $runKeyPath -Name $runValueName -ErrorAction SilentlyContinue
    if ($null -ne $remainingValue) {
        throw 'Startup registry value remained after disabling the option.'
    }
    Write-Output 'PASS: disabling startup removes the registry value'
} finally {
    if ($null -ne $hostProcess -and -not $hostProcess.HasExited) {
        try { Send-HostCommand -HostPid $hostProcess.Id -Command 'quit-host' | Out-Null } catch {}
        if (-not $hostProcess.WaitForExit(5000)) { Stop-Process -Id $hostProcess.Id -Force }
    }

    if ($originalExists) {
        $restoreKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey(
            'Software\Microsoft\Windows\CurrentVersion\Run',
            $true
        )
        try { $restoreKey.SetValue($runValueName, $originalValue, $originalKind) } finally { $restoreKey.Dispose() }
    } else {
        Remove-ItemProperty -LiteralPath $runKeyPath -Name $runValueName -ErrorAction SilentlyContinue
    }
}
