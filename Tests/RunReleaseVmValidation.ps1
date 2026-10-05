<#
Run in 64-bit elevated PowerShell INSIDE a disposable Windows VM after saving a
restorable snapshot. Keep this file, InstallerLifecycleTests.ps1, both MSI
packages, HostProtocol.ps1 and host-computer-name.txt (the host name recorded when bundling) in one
folder. Without -Execute, only package inspection/plans run.

Example (replace the name with the VM's actual computer name, shown by hostname):
  .\RunReleaseVmValidation.ps1 -DisposableComputerName 'RELEASE-VM' -Execute

Copy the results directory out before reverting the VM snapshot. This runner
does not create/revert snapshots or authorize execution based on the local name.
#>
param(
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$DisposableComputerName,
    [string]$ProhibitedComputerName='',
    [string]$CurrentMsi='',
    [string]$PreviousMsi='',
    [string]$OutputDirectory='',
    [string]$CustomInstallDirectory='',
    [switch]$Execute
)
$ErrorActionPreference='Stop'
if (!$CurrentMsi) { $CurrentMsi=Join-Path $PSScriptRoot 'LightHostModern-2.0.0-Setup.msi' }
if (!$PreviousMsi) { $PreviousMsi=Join-Path $PSScriptRoot 'LightHostModern-1.4.1-Setup.msi' }
if (!$CustomInstallDirectory) { $CustomInstallDirectory=Join-Path $env:ProgramFiles 'LightHostModern MSI Validation' }
function Assert-ValidationComputer([string]$RunningName,[string]$ExpectedName,[string]$ForbiddenName) {
    if (!$ForbiddenName -or $ForbiddenName.Equals($RunningName,[StringComparison]::OrdinalIgnoreCase) -or
        $ForbiddenName.Equals($ExpectedName,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Execution is forbidden on the original host. Supply its recorded name and a different disposable VM computer name.'
    }
    if (!$ExpectedName -or !$ExpectedName.Equals($RunningName,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'The supplied disposable computer name does not match this machine. Run only inside the named VM.'
    }
}
$hostNameRecord=Join-Path $PSScriptRoot 'host-computer-name.txt'
if (Test-Path -LiteralPath $hostNameRecord -PathType Leaf) {
    $recordedHost=([IO.File]::ReadAllText($hostNameRecord)).Trim()
    if ($ProhibitedComputerName -and !$ProhibitedComputerName.Equals($recordedHost,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'The prohibited host name does not match the name recorded in this bundle.'
    }
    $ProhibitedComputerName=$recordedHost
}
if ($Execute) {
    Assert-ValidationComputer $env:COMPUTERNAME $DisposableComputerName $ProhibitedComputerName
    $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or ![Environment]::Is64BitProcess) {
        throw 'Run 64-bit PowerShell as administrator inside the named disposable VM.'
    }
}
$harness=Join-Path $PSScriptRoot 'InstallerLifecycleTests.ps1'
foreach ($required in @($harness,(Join-Path $PSScriptRoot 'HostProtocol.ps1'),$CurrentMsi,$PreviousMsi)) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing validation input: $required" }
}
$CurrentMsi=(Resolve-Path -LiteralPath $CurrentMsi).Path
$PreviousMsi=(Resolve-Path -LiteralPath $PreviousMsi).Path
$inputsVerified=$false
if ($Execute) {
    # Execution consumes only the exact reviewed bundle. Dry plans can still
    # inspect repository builds before the final inventory has been generated.
    $allowedNames=@('RunReleaseVmValidation.ps1','InstallerLifecycleTests.ps1','HostProtocol.ps1',
        'LightHostModern-2.0.0-Setup.msi','LightHostModern-1.4.1-Setup.msi','host-computer-name.txt','README.txt')
    $inventoryPath=Join-Path $PSScriptRoot 'inputs.json'
    if (!(Test-Path -LiteralPath $inventoryPath -PathType Leaf) -or (Get-Item -LiteralPath $inventoryPath).Length -gt 65536) {
        throw 'Execution requires the final bundle inputs.json inventory.'
    }
    $inventory=Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
    $seen=@{}
    foreach ($entry in $inventory) {
        if ($entry.name -isnot [string] -or $entry.name -notin $allowedNames -or $seen.ContainsKey($entry.name) -or
            [string]$entry.size -notmatch '^[0-9]+$' -or [string]$entry.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
            throw 'The bundle inventory contains an unexpected, duplicated or invalid entry.'
        }
        $seen[$entry.name]=$true
        $path=Join-Path $PSScriptRoot $entry.name
        $cursor=[IO.Path]::GetFullPath($path)
        while ($cursor) {
            if ((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw 'Bundle inputs cannot be links or pass through reparse points.'
            }
            $cursor=[IO.Path]::GetDirectoryName($cursor)
        }
        $file=Get-Item -LiteralPath $path
        if ($file.PSIsContainer -or $file.Length -ne [long]$entry.size -or
            (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ine $entry.sha256) {
            throw "Bundle file does not match inputs.json: $($entry.name)"
        }
    }
    if ($seen.Count -ne $allowedNames.Count) { throw 'The bundle inventory is incomplete.' }
    if ($CurrentMsi -ine [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'LightHostModern-2.0.0-Setup.msi')) -or
        $PreviousMsi -ine [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'LightHostModern-1.4.1-Setup.msi'))) {
        throw 'Execution must use the MSI files verified in this bundle.'
    }
    $inputsVerified=$true
}
if (!$OutputDirectory) { $OutputDirectory=Join-Path $PSScriptRoot ('results-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)) }
$outputRoot=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
if (Test-Path -LiteralPath $outputRoot) { throw 'Choose a new results directory; previous evidence is never overwritten.' }
$customRoot=[IO.Path]::GetFullPath($CustomInstallDirectory).TrimEnd('\')
$programFiles=[IO.Path]::GetFullPath($env:ProgramFiles).TrimEnd('\')
if (!$customRoot.StartsWith($programFiles+'\',[StringComparison]::OrdinalIgnoreCase) -or
    $customRoot.Equals((Join-Path $programFiles 'LightHostModern'),[StringComparison]::OrdinalIgnoreCase)) {
    throw 'The custom round requires a distinct installation directory below Program Files.'
}
New-Item -ItemType Directory -Path $outputRoot | Out-Null
$rounds=[Collections.Generic.List[object]]::new()
$summary=[ordered]@{
    requestedExecution=[bool]$Execute;computer=$env:COMPUTERNAME;disposableComputer=$DisposableComputerName;prohibitedComputer=$ProhibitedComputerName;
    currentMsi=$CurrentMsi;previousMsi=$PreviousMsi;customInstallDirectory=$customRoot;inputsVerified=$inputsVerified;
    status='running';error='';rounds=@()
}
function Command-Literal([string]$Value) { "'"+$Value.Replace("'","''")+"'" }
try {
    foreach ($round in @(
        [pscustomobject]@{name='default';installDirectory=''},
        [pscustomobject]@{name='custom';installDirectory=$customRoot})) {
        $roundOutput=Join-Path $outputRoot $round.name
        $command="& $(Command-Literal $harness) -CurrentMsi $(Command-Literal $CurrentMsi) -PreviousMsi $(Command-Literal $PreviousMsi) -DisposableComputerName $(Command-Literal $DisposableComputerName) -OutputDirectory $(Command-Literal $roundOutput) -CleanupPreferenceFixture"
        if ($round.installDirectory) { $command+=" -InstallDirectory $(Command-Literal $round.installDirectory)" }
        if ($Execute) { $command+=' -Execute' }
        $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
        Write-Host "Starting $($round.name) round. Evidence: $roundOutput"
        $process=Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" `
            -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded) `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $outputRoot ($round.name+'-stdout.log')) `
            -RedirectStandardError (Join-Path $outputRoot ($round.name+'-stderr.log'))
        $null=$process.Handle
        if (!$process.WaitForExit(1800000)) {
            throw "The $($round.name) runner (PID $($process.Id)) is still active. It was not killed; inspect it before continuing."
        }
        $resultPath=Join-Path $roundOutput 'results.json'
        $result=if (Test-Path -LiteralPath $resultPath) { Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json } else { $null }
        $rounds.Add([pscustomobject]@{name=$round.name;exitCode=$process.ExitCode;results=$resultPath;installDirectory=$round.installDirectory})
        if ($process.ExitCode -ne 0 -or !$result) { throw "The $($round.name) round failed; inspect its stdout/stderr and MSI logs. No later round was started." }
        if ($Execute) {
            if (!$result.executed -or @($result.scenarios).Count -lt 7 -or @($result.scenarios | Where-Object status -ne 'passed').Count -or !$result.preferenceFixture.cleaned -or
                !@($result.installedPayloadInventories.clean).Count -or !@($result.installedPayloadInventories.upgrade).Count -or
                @($result.startupSmokes).Count -ne 3 -or @($result.startupSmokes | Where-Object { $_.status -ne 'passed' -or !$_.normalExit }).Count) {
                throw "The $($round.name) round did not finish all lifecycle and fixture cleanup checks."
            }
            if (@($result.installerOutcomes | Where-Object rebootRequired).Count) {
                throw 'Windows Installer requested a restart. Reboot/restore the disposable VM and review the logs before continuing.'
            }
        }
        Write-Host "PASS: $($round.name) round"
    }
    $summary.status=if ($Execute) { 'passed' } else { 'planned-not-executed' }
} catch {
    $summary.status='failed'; $summary.error=$_.Exception.Message
    throw
} finally {
    $summary.rounds=@($rounds)
    $summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputRoot 'vm-validation-results.json') -Encoding UTF8
}
Write-Host "Results: $outputRoot"
