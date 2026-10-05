# Exercises the interactive backup with a disposable key, never the maintainer key.
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$probeId = [Guid]::NewGuid().ToString('N')
$probeRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) ('LightHostModern-backup-test-' + $probeId)))
$probeCertificate = $null
$probeThumbprint = $null
$script:probeSecret = $null
$probeCreated = $false
try {
    if (Test-Path -LiteralPath $probeRoot) { throw 'The disposable test directory already exists.' }
    $null = New-Item -ItemType Directory -Path $probeRoot
    $probeCreated = $true
    $script:probeDestination = Join-Path $probeRoot 'fixture-only.pfx'
    $random = New-Object byte[] 32
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    try { $rng.GetBytes($random) } finally { $rng.Dispose() }
    $script:probeSecret = New-Object Security.SecureString
    foreach ($b in $random) { $script:probeSecret.AppendChar([char](33 + ($b % 90))) }
    [Array]::Clear($random, 0, $random.Length)
    $script:probeSecret.MakeReadOnly()
    $parameters = @{
        Type = 'Custom'; Subject = 'CN=LightHostModern Update Manifest Signing'
        FriendlyName = 'LightHostModern Update Manifest Signing'
        KeyFriendlyName = 'Disposable backup test ' + $probeId
        CertStoreLocation = 'Cert:\CurrentUser\My'
        Provider = 'Microsoft Software Key Storage Provider'
        KeyAlgorithm = 'RSA'; KeyLength = 3072; HashAlgorithm = 'SHA256'
        KeyUsage = 'DigitalSignature'; KeyUsageProperty = 'Sign'
        KeyExportPolicy = 'ExportableEncrypted'; NotAfter = (Get-Date).AddDays(1)
    }
    $probeCertificate = New-SelfSignedCertificate @parameters
    $probeThumbprint = $probeCertificate.Thumbprint
    $probeContext = [pscustomobject]@{ Secret = $script:probeSecret; Destination = $script:probeDestination }
    Set-Item -Path Function:\Read-Host -Value ({
        param([string] $Prompt, [switch] $AsSecureString)
        if ($AsSecureString) { return $probeContext.Secret.Copy() }
        return $probeContext.Destination
    }.GetNewClosure())
    & (Join-Path $repo 'Utilities\Backup Update Signing Key.ps1') -Thumbprint $probeThumbprint
    if (!(Test-Path -LiteralPath $script:probeDestination)) { throw 'Backup did not publish the encrypted fixture.' }
    $data = Get-PfxData -FilePath $script:probeDestination -Password $script:probeSecret
    try {
        if (@($data.EndEntityCertificates).Count -ne 1 -or $data.EndEntityCertificates[0].Thumbprint -ne $probeThumbprint) { throw 'Backup does not contain the disposable test certificate.' }
    } finally {
        foreach ($item in @($data.EndEntityCertificates) + @($data.OtherCertificates)) { if ($item) { $item.Dispose() } }
    }
    $hashBefore = (Get-FileHash -LiteralPath $script:probeDestination).Hash
    $rejected = $false
    try { & (Join-Path $repo 'Utilities\Backup Update Signing Key.ps1') -Thumbprint $probeThumbprint } catch { $rejected = $true }
    if (!$rejected -or (Get-FileHash -LiteralPath $script:probeDestination).Hash -ne $hashBefore) { throw 'Existing backup was not preserved.' }
    $script:probeDestination = Join-Path $repo ('out\forbidden-key-fixture-' + $probeId + '.pfx')
    $probeContext.Destination = $script:probeDestination
    $rejected = $false
    try { & (Join-Path $repo 'Utilities\Backup Update Signing Key.ps1') -Thumbprint $probeThumbprint } catch { $rejected = $true }
    if (!$rejected -or (Test-Path -LiteralPath $script:probeDestination)) { throw 'Private-key backup was allowed inside the repository.' }
    'PASS: encrypted backup, password confirmation, existing-file preservation and repository exclusion (disposable key only).'
} finally {
    if ($script:probeSecret) { $script:probeSecret.Dispose() }
    if ($probeThumbprint) {
        # Only the certificate returned by this test's creation call is owned.
        $owned = Get-Item -LiteralPath ('Cert:\CurrentUser\My\' + $probeThumbprint) -ErrorAction SilentlyContinue
        if ($owned) { Remove-Item -LiteralPath ('Cert:\CurrentUser\My\' + $probeThumbprint) -DeleteKey -Force }
    }
    if ($probeCertificate) { $probeCertificate.Dispose() }
    if ($probeCreated) {
        # No recursive cleanup: unexpected content remains for inspection.
        $expected = Join-Path $probeRoot 'fixture-only.pfx'
        if ([IO.File]::Exists($expected)) { [IO.File]::Delete($expected) }
        [IO.Directory]::Delete($probeRoot, $false)
    }
}
