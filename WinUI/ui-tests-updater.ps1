param([string] $PackageDirectory = 'out/release-test-completion-g', [string] $OutputDirectory = 'out/ui-updater')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\Tests\HostProtocol.ps1"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$packageMetadata = Get-Content -LiteralPath (Join-Path $PackageDirectory 'release-artifacts.json') -Raw | ConvertFrom-Json
$portableArtifacts = @($packageMetadata.artifacts | Where-Object distribution -CEQ 'portable')
if ($packageMetadata.formatVersion -ne 1 -or $portableArtifacts.Count -ne 1) { throw 'Expected one portable release artifact in package metadata.' }
$portableArtifact = $portableArtifacts[0]
$packageVersion = [string]$portableArtifact.version
if ($packageVersion -notmatch '^\d+\.\d+\.\d+$') { throw 'Portable release version must contain three numeric parts.' }
$portableName = "LightHostModern-v$packageVersion-Portable.zip"
if ($portableArtifact.name -cne $portableName -or $portableArtifact.architecture -ne 'x64') { throw 'Unexpected versioned portable artifact identity.' }
$root = Join-Path $repo 'out\test-profiles'
$testProfileName = 'ui-update-' + [guid]::NewGuid().ToString('N')
$profile = Join-Path $root $testProfileName
$temp = Join-Path $profile 'Temp'
$sourceDir = Join-Path $temp 'Source'
New-Item -ItemType Directory -Path $sourceDir -Force | Out-Null
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$sourceZip = Join-Path $sourceDir $portableName
Copy-Item -LiteralPath (Join-Path $PackageDirectory $portableName) -Destination $sourceZip
$sourceInfo = Get-Item -LiteralPath $sourceZip
$sourceDigest = 'sha256:' + (Get-FileHash -LiteralPath $sourceZip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($sourceInfo.Length -ne $portableArtifact.size -or $sourceDigest -cne $portableArtifact.digest) { throw 'Portable package differs from release metadata.' }
function Write-Fixture([bool] $CorruptDigest = $false) {
    $digest = if ($CorruptDigest) { 'sha256:' + ('0' * 64) } else { $sourceDigest }
    [ordered]@{
        tag_name = "v$packageVersion"; html_url = "https://github.com/heide-oficial/Light-Host-Modern/releases/tag/v$packageVersion"
        reportedCurrentVersion = '0.0.0'; localPackage = $sourceZip; chunkDelayMs = 20
        assets = @(@{ name = $portableName; size = $sourceInfo.Length; digest = $digest
            browser_download_url = "https://github.com/heide-oficial/Light-Host-Modern/releases/download/v$packageVersion/$portableName" })
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $temp 'update-fixture.json') -Encoding UTF8
}
Write-Fixture
$hostExe = Join-Path (Get-TestBuildDirectory) 'LightHostModern_artefacts\Release\LightHostModern.exe'
$hostProcess = $null
$pipe = ''
$session = ''
function Start-Host {
    $script:hostProcess = Start-Process -FilePath $hostExe -ArgumentList @("--test-profile=$testProfileName", ('--profile-root="' + $root + '"')) -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (!(Test-Path -LiteralPath (Join-Path $profile 'profile.json'))) {
        if ($hostProcess.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw 'Isolated update host did not start.' }
        Start-Sleep -Milliseconds 100
    }
    $script:pipe = (Get-Content -LiteralPath (Join-Path $profile 'profile.json') -Raw | ConvertFrom-Json).pipe
    $script:session = (Send-HostRequest $pipe 'hello').hostSession
}
$script:AppPid = 0
function UI([string[]] $Arguments) {
    $result = & winapp ui @Arguments -a $script:AppPid --json
    if ($LASTEXITCODE -ne 0) { throw "$result" }
    $result | ConvertFrom-Json
}
function Open-UI {
    $launch = Start-TestUi -Directory "WinUI/x64/Release/LightHostModern.WinUI" -Arguments @("--test-profile=$testProfileName", "--profile-root=$root", "--host-pipe=$pipe")
    if ($LASTEXITCODE -ne 0) { throw 'Could not open update UI.' }
    $script:AppPid = $launch.ProcessId
    @{hostPid=$hostProcess.Id; uiPid=$script:AppPid; profile=$profile; pipe=$pipe; name=$testProfileName; root=$root} | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $OutputDirectory 'profile.json') -Encoding UTF8
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        try { UI @('invoke', 'NavSettings') | Out-Null; UI @('wait-for', 'InstallUpdate', '-t', '2500') | Out-Null; return }
        catch { if ([DateTime]::UtcNow -ge $deadline) { throw }; Start-Sleep -Milliseconds 200 }
    } while ($true)
}
function Close-UI {
    $process = Get-Process -Id $script:AppPid
    UI @('invoke', 'Close') | Out-Null
    if (!$process.WaitForExit(15000)) { throw 'UI did not exit after cancellation.' }
}
function Assert-Host {
    $live = Send-HostRequest $pipe 'snapshot'
    if ($live.hostSession -ne $session -or $live.globalMuted -or $live.globalBypassed -or $live.audioConfig.currentInputDeviceIndex -ge 0 -or $live.audioConfig.currentOutputDeviceIndex -ge 0) {
        throw 'Portable update changed the live host or opened a device.'
    }
}
function Partials {
    @(Get-ChildItem -LiteralPath (Join-Path $temp 'Updates') -Recurse -File -Filter '*.partial' -ErrorAction SilentlyContinue)
}
$results = [Collections.Generic.List[object]]::new()
function Scenario([string] $Name, [scriptblock] $Work) {
    try { & $Work; $results.Add([pscustomobject]@{name=$Name; status='passed'}) }
    catch { $results.Add([pscustomobject]@{name=$Name; status='failed'; error=$_.Exception.Message}) }
}
try {
Start-Host
Open-UI
Scenario 'Portable update shows its package type and cancellation deletes the partial transfer' {
    $label = UI @('get-property', 'InstallUpdate', '-p', 'Name')
    if (($label | ConvertTo-Json) -notmatch 'Download portable ZIP') { throw 'Portable package action was not shown.' }
    UI @('invoke', 'InstallUpdate') | Out-Null
    UI @('wait-for', 'UpdateTransferBytes', '-t', '4000') | Out-Null
    UI @('screenshot', '-o', "$OutputDirectory/downloading.png") | Out-Null
    UI @('invoke', 'CancelUpdate') | Out-Null
    UI @('wait-for', 'CancelUpdate', '--gone', '-t', '10000') | Out-Null
    if ((Partials).Count) { throw 'Cancelled download left a partial file.' }
    Assert-Host
    UI @('screenshot', '-o', "$OutputDirectory/cancelled.png") | Out-Null
}
Scenario 'Completed ZIP is validated, offered to the user and keeps host and UI running' {
    UI @('invoke', 'InstallUpdate') | Out-Null
    UI @('wait-for', 'CancelUpdate', '--gone', '-t', '45000') | Out-Null
    $label = UI @('get-property', 'InstallUpdate', '-p', 'Name')
    if (($label | ConvertTo-Json) -notmatch 'Show downloaded ZIP') { throw 'Verified portable package was not offered.' }
    $package = Get-ChildItem -LiteralPath (Join-Path $temp 'Updates') -Recurse -File -Filter $portableName | Select-Object -Last 1
    if (!$package -or ('sha256:' + (Get-FileHash -LiteralPath $package.FullName -Algorithm SHA256).Hash.ToLowerInvariant()) -ne $sourceDigest) { throw 'Downloaded ZIP content differs.' }
    $validation = Get-Content -LiteralPath (Join-Path $package.DirectoryName 'update-result.json') -Raw | ConvertFrom-Json
    if ($validation.state -ne 'validated') { throw 'Helper did not validate the ZIP.' }
    Assert-Host
    UI @('screenshot', '-o', "$OutputDirectory/portable-ready.png") | Out-Null
}
Scenario 'Checksum mismatch is visible and no incomplete package is offered' {
    Close-UI
    Write-Fixture $true
    Open-UI
    UI @('invoke', 'InstallUpdate') | Out-Null
    UI @('wait-for', 'CancelUpdate', '--gone', '-t', '45000') | Out-Null
    $error = UI @('search', 'The package checksum did not match')
    if (!$error.matches.Count) { throw 'Checksum failure was not displayed.' }
    if ((Partials).Count) { throw 'Checksum failure left a partial file.' }
    Assert-Host
    UI @('screenshot', '-o', "$OutputDirectory/checksum-error.png") | Out-Null
}
Scenario 'Closing the UI during transfer cancels the worker and preserves the host' {
    UI @('invoke', 'InstallUpdate') | Out-Null
    UI @('wait-for', 'UpdateTransferBytes', '-t', '4000') | Out-Null
    Close-UI
    if ((Partials).Count) { throw 'Closing UI left a partial file.' }
    Assert-Host
    Write-Fixture
    Open-UI
}
} finally {
    try {
        if ($script:AppPid -and (Get-Process -Id $script:AppPid -ErrorAction SilentlyContinue)) { Close-UI }
    } catch { $results.Add([pscustomobject]@{name='UI shutdown';status='failed';error=$_.Exception.Message}) }
    try {
        if ($hostProcess -and !$hostProcess.HasExited) {
            if (!$pipe) {
                $script:pipe = (Get-Content -LiteralPath (Join-Path $profile 'profile.json') -Raw | ConvertFrom-Json).pipe
            }
            if (!$session) { $script:session = (Send-HostRequest $pipe 'hello').hostSession }
            Send-HostRequest $pipe 'quit-host' -Session $session | Out-Null
            if (!$hostProcess.WaitForExit(15000)) { throw 'The isolated update host did not exit.' }
        }
    } catch { $results.Add([pscustomobject]@{name='Host shutdown';status='failed';error=$_.Exception.Message}) }
    $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'results.json') -Encoding UTF8
}
$results | Format-Table -AutoSize
if (@($results | Where-Object { $_.status -eq 'failed' }).Count) { exit 1 }
