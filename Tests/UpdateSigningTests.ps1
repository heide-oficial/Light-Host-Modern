param(
    [Parameter(Mandatory)][string] $Thumbprint,
    [string] $VerifierExecutable = '',
    [string] $OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $repo 'Utilities/Update Signing.ps1')
if (!$VerifierExecutable) { $VerifierExecutable = Join-Path $repo 'out/build/windows-vs2022/Release/LightHostModernUpdateTests.exe' }
$verifier = (Get-Item -LiteralPath $VerifierExecutable).FullName
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repo ('out/signing-setup-test-' + [guid]::NewGuid().ToString('N')) }
$root = [IO.Path]::GetFullPath($OutputDirectory)
$allowed = [IO.Path]::GetFullPath((Join-Path $repo 'out')).TrimEnd('\') + '\'
if (!$root.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Signing fixtures must stay inside the workspace out directory.' }
if (Test-Path -LiteralPath $root) { throw 'Use a new output directory for signing fixtures.' }
for ($parent = $root; $parent; $parent = [IO.Path]::GetDirectoryName($parent)) {
    if ((Test-Path -LiteralPath $parent) -and ((Get-Item -LiteralPath $parent).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Signing fixture paths cannot contain reparse points.' }
}
New-Item -ItemType Directory -Path $root | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $root 'FIXTURE-ONLY.txt'), 'Local signature-verification fixture. Version 0.0.0, no artifacts, no package, no installation. Never publish these files.', $utf8)
$valid = Join-Path $root 'valid'
New-Item -ItemType Directory -Path $valid | Out-Null
# Only the configured certificate signs. The key stays in the Windows store;
# no new key, private export, temporary root or compiler trust override is used.
Write-SignedUpdateManifest -OutputDirectory $valid -Version '0.0.0' -Artifacts @() -InventoryHash '' -Thumbprint $Thumbprint
$body = [IO.File]::ReadAllText((Join-Path $valid 'update-manifest.json'), $utf8)
$signature = [IO.File]::ReadAllText((Join-Path $valid 'update-manifest.sig'), $utf8)
$manifest = $body | ConvertFrom-Json
if ($manifest.version -ne '0.0.0' -or @($manifest.artifacts).Count -ne 0) { throw 'Signing test fixture unexpectedly contains an installable artifact.' }
$results = [Collections.Generic.List[object]]::new()
function Verify-Case([string] $Name, [string] $Body, [string] $Signature, [string] $Expected) {
    $case = Join-Path $root $Name
    if ($Name -ne 'valid') { New-Item -ItemType Directory -Path $case | Out-Null }
    [IO.File]::WriteAllText((Join-Path $case 'update-manifest.json'), $Body, $utf8)
    [IO.File]::WriteAllText((Join-Path $case 'update-manifest.sig'), $Signature, $utf8)
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $verifier
    $start.Arguments = '--verify-configured-manifest "' + $case + '"'
    $start.WorkingDirectory = $case
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    try {
        if (!$process.Start()) { throw 'Could not start configured-root verifier.' }
        $output = $process.StandardOutput.ReadToEndAsync()
        $errors = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(10000)) { $process.Kill(); $process.WaitForExit(); throw 'Configured-root verifier exceeded its deadline.' }
        $message = ($output.Result + $errors.Result).Trim()
        $exitCode = $process.ExitCode
        $expectedExit = if ($Expected -eq 'configured_manifest_validated') { 0 } else { 1 }
        $passed = $exitCode -eq $expectedExit -and $message -eq $Expected
        $results.Add([ordered]@{ name = $Name; passed = $passed; expected = $Expected; actual = $message; exitCode = $exitCode })
        if ($passed) { Write-Host "PASS: $Name ($Expected)" } else { Write-Warning "FAIL: $Name expected $Expected, received $message (exit $exitCode)" }
    } finally { $process.Dispose() }
}
Verify-Case 'valid' $body $signature 'configured_manifest_validated'
# Whitespace preserves JSON semantics but changes the exact signed bytes.
Verify-Case 'altered-body' ($body + "`n ") $signature 'signature_invalid'
$replacement = if ($signature[0] -eq '0') { '1' } else { '0' }
Verify-Case 'altered-signature' $body ($replacement + $signature.Substring(1)) 'signature_invalid'
$unknown = $body | ConvertFrom-Json
$unknown.keyId = 'untrusted-fixture-' + [guid]::NewGuid().ToString('N')
Verify-Case 'unknown-key' ($unknown | ConvertTo-Json -Depth 6) $signature 'signature_untrusted'
$passed = @($results | Where-Object { !$_.passed }).Count -eq 0
$report = [ordered]@{
    passed = $passed; fixtureOnly = $true; installableArtifacts = 0; version = '0.0.0'
    trustSource = 'compiled trustedUpdateKeys (default verifier arguments)'
    keyId = $manifest.keyId; verifier = $verifier
    verifierSha256 = (Get-FileHash -LiteralPath $verifier -Algorithm SHA256).Hash.ToLowerInvariant()
    scenarios = @($results.ToArray())
}
[IO.File]::WriteAllText((Join-Path $root 'result.json'), ($report | ConvertTo-Json -Depth 8), $utf8)
Write-Host "Signature test results: $root"
if (!$passed) { exit 1 }
