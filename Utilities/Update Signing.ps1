# Dot-source to sign manifests or export a PUBLIC trust-key entry. Uses the
# Windows certificate store; no private key is exported or written to the repo.
function Get-ConfiguredManifestSigningThumbprint {
    $configured = [Environment]::GetEnvironmentVariable('LIGHTHOST_MANIFEST_SIGNING_THUMBPRINT', 'Process')
    if ([string]::IsNullOrWhiteSpace($configured)) {
        $configured = [Environment]::GetEnvironmentVariable('LIGHTHOST_MANIFEST_SIGNING_THUMBPRINT', 'User')
    }
    return $configured
}
function Get-UpdateSigningIdentity([string] $Thumbprint) {
    if ([string]::IsNullOrWhiteSpace($Thumbprint)) { $Thumbprint = Get-ConfiguredManifestSigningThumbprint }
    $Thumbprint = ($Thumbprint -replace '\s', '').ToUpperInvariant()
    if ($Thumbprint -notmatch '^[0-9A-F]{40}$') { throw 'Specify the thumbprint of the update signing certificate.' }
    $certificate = Get-Item -LiteralPath ('Cert:\CurrentUser\My\' + $Thumbprint) -ErrorAction SilentlyContinue
    if (!$certificate) { $certificate = Get-Item -LiteralPath ('Cert:\LocalMachine\My\' + $Thumbprint) }
    $rsa = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($certificate)
    if (!$rsa -or $rsa.KeySize -lt 2048) { throw 'Update manifests require RSA 2048 bits or stronger.' }
    try { $parameters = $rsa.ExportParameters($false) } finally { $rsa.Dispose() }
    $stream = New-Object IO.MemoryStream
    $writer = New-Object IO.BinaryWriter($stream)
    try {
        $writer.Write([uint32]0x31415352); $writer.Write([uint32]($parameters.Modulus.Length * 8))
        $writer.Write([uint32]$parameters.Exponent.Length); $writer.Write([uint32]$parameters.Modulus.Length)
        $writer.Write([uint32]0); $writer.Write([uint32]0)
        $writer.Write([byte[]]$parameters.Exponent); $writer.Write([byte[]]$parameters.Modulus); $writer.Flush()
        $blob = $stream.ToArray()
    } finally { $writer.Dispose(); $stream.Dispose() }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $keyId = ([BitConverter]::ToString($sha.ComputeHash($blob))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    return @{ Certificate = $certificate; KeyId = $keyId; PublicBlobHex = ([BitConverter]::ToString($blob)).Replace('-','').ToLowerInvariant() }
}
function Assert-UpdateSigningTrust([string] $Thumbprint, [string] $TrustHeader) {
    $identity = Get-UpdateSigningIdentity $Thumbprint
    if (!$identity.Certificate.HasPrivateKey) { throw 'The update signing certificate has no private key on this computer.' }
    $source = [IO.File]::ReadAllText($TrustHeader)
    $entry = '\{\s*"' + [regex]::Escape($identity.KeyId) + '"\s*,\s*"' + [regex]::Escape($identity.PublicBlobHex) + '"\s*\}'
    if ($source -notmatch $entry) { throw 'The manifest signing key is not in UpdateTrustKeys.h. Review the public key and rebuild before releasing.' }
}
function Write-SignedUpdateManifest([string] $OutputDirectory, [string] $Version, $Artifacts, [string] $InventoryHash, [string] $Thumbprint) {
    $identity = Get-UpdateSigningIdentity $Thumbprint
    $manifest = [ordered]@{ formatVersion = 1; keyId = $identity.KeyId; version = $Version; architecture = 'x64';
        minimumLauncher = 1; portableInventorySha256 = $InventoryHash; artifacts = @($Artifacts) } | ConvertTo-Json -Depth 6
    $utf8 = New-Object Text.UTF8Encoding($false); $bytes = $utf8.GetBytes($manifest)
    $rsa = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($identity.Certificate)
    if (!$rsa) { throw 'Manifest signing key is not available.' }
    try { $signature = $rsa.SignData($bytes, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1) } finally { $rsa.Dispose() }
    [IO.File]::WriteAllBytes((Join-Path $OutputDirectory 'update-manifest.json'), $bytes)
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'update-manifest.sig'), ([BitConverter]::ToString($signature)).Replace('-','').ToLowerInvariant(), $utf8)
}
