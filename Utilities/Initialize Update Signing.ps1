[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Update Signing.ps1')
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$header = Join-Path $repo 'Source\UpdateTrustKeys.h'
$subject = 'CN=LightHostModern Update Manifest Signing'
$friendlyName = 'LightHostModern Update Manifest Signing'
$oldSource = [IO.File]::ReadAllText($header)
$emptyTrust = $oldSource -match 'std::array<TrustedUpdateKey,\s*0>\s+trustedUpdateKeys\s*\{\s*\}'
$thumbprint = Get-ConfiguredManifestSigningThumbprint
$certificate = $null
if (![string]::IsNullOrWhiteSpace($thumbprint)) {
    $thumbprint = ($thumbprint -replace '\s', '').ToUpperInvariant()
    if ($thumbprint -notmatch '^[0-9A-F]{40}$') { throw 'The configured update signing thumbprint is invalid.' }
    $certificate = Get-Item -LiteralPath ('Cert:\CurrentUser\My\' + $thumbprint) -ErrorAction Stop
} else {
    $existing = @(Get-ChildItem -LiteralPath 'Cert:\CurrentUser\My' | Where-Object { $_.Subject -eq $subject -and $_.FriendlyName -eq $friendlyName })
    if ($existing.Count -gt 1) { throw 'Multiple dedicated certificates exist. Select the existing certificate explicitly; do not regenerate a signing key.' }
    if ($existing.Count -eq 1) { $certificate = $existing[0] }
}
if (!$certificate) {
    if (!$emptyTrust) { throw 'Trust is already configured. Restore its original signing key; this bootstrap must not rotate it.' }
    $parameters = @{
        Type = 'Custom'
        Subject = $subject
        FriendlyName = $friendlyName
        KeyFriendlyName = $friendlyName
        CertStoreLocation = 'Cert:\CurrentUser\My'
        Provider = 'Microsoft Software Key Storage Provider'
        KeyAlgorithm = 'RSA'
        KeyLength = 3072
        HashAlgorithm = 'SHA256'
        KeyUsage = 'DigitalSignature'
        KeyUsageProperty = 'Sign'
        KeyExportPolicy = 'ExportableEncrypted'
        NotAfter = (Get-Date).AddYears(10)
    }
    $certificate = New-SelfSignedCertificate @parameters
}
if ($certificate.Subject -ne $subject -or $certificate.FriendlyName -ne $friendlyName -or !$certificate.HasPrivateKey) {
    throw 'Use only the dedicated LightHostModern update signing certificate with its private key.'
}
$identity = Get-UpdateSigningIdentity $certificate.Thumbprint

# Prove that this account can sign and that the public identity matches. Only a
# fixed, non-manifest challenge is signed here; private parameters are never read.
$challenge = [Text.Encoding]::UTF8.GetBytes('LightHostModern update key setup verification v1')
$privateRsa = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($certificate)
$publicRsa = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($certificate)
try {
    if (!$privateRsa -or $privateRsa.KeySize -lt 3072) { throw 'A dedicated RSA key of at least 3072 bits is required.' }
    if ($privateRsa -is [Security.Cryptography.RSACng]) {
        $policy = $privateRsa.Key.ExportPolicy
        if (($policy -band [Security.Cryptography.CngExportPolicies]::AllowPlaintextExport) -ne 0) { throw 'The dedicated key must not allow plaintext export.' }
        if (($policy -band [Security.Cryptography.CngExportPolicies]::AllowExport) -eq 0) { throw 'The dedicated key must allow an encrypted recovery backup.' }
    }
    $signature = $privateRsa.SignData($challenge, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    if (!$publicRsa.VerifyData($challenge, $signature, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)) {
        throw 'Signing key verification failed.'
    }
} finally {
    if ($privateRsa) { $privateRsa.Dispose() }
    if ($publicRsa) { $publicRsa.Dispose() }
}

if ($emptyTrust) {
    $newSource = @"
#pragma once
#include <array>
#include <string_view>

namespace lightHostModern::update
{
struct TrustedUpdateKey { std::string_view id, publicBlobHex; };
// Public RSA key for update manifests, not an Authenticode certificate.
// Private signing material remains in the maintainer's Windows certificate store.
// The stable portable launcher embeds this trust: plan rotation before publishing.
inline constexpr std::array<TrustedUpdateKey, 1> trustedUpdateKeys{{
    {"$($identity.KeyId)", "$($identity.PublicBlobHex)"}
}};
}
"@
    # Do not silently replace a key inserted while setup was running.
    if ([IO.File]::ReadAllText($header) -ne $oldSource) { throw 'The trust header changed during setup. Review it before continuing.' }
    [IO.File]::WriteAllText($header, $newSource + "`r`n", (New-Object Text.UTF8Encoding($false)))
}
Assert-UpdateSigningTrust $certificate.Thumbprint $header

# This is a PUBLIC identifier, not the signing secret. Persisting it allows a
# newly opened build shell to find the same key without exporting private data.
[Environment]::SetEnvironmentVariable('LIGHTHOST_MANIFEST_SIGNING_THUMBPRINT', $certificate.Thumbprint, 'User')
$env:LIGHTHOST_MANIFEST_SIGNING_THUMBPRINT = $certificate.Thumbprint
[pscustomobject]@{
    Thumbprint = $certificate.Thumbprint
    KeyId = $identity.KeyId
    Store = 'Cert:\CurrentUser\My'
    Algorithm = 'RSA-3072 / SHA-256'
    PublicTrustHeader = $header
    Backup = 'Not exported. Run Utilities\Backup Update Signing Key.ps1 interactively and store the encrypted backup offline.'
}
