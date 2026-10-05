#requires -Version 5.1
# Interactive backup only. Never accepts a password in arguments or environment.
[CmdletBinding()]
param([string] $Thumbprint = '')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSEdition -ne 'Desktop') { throw 'Execute este script com Windows PowerShell 5.1 (powershell.exe).' }

if ([string]::IsNullOrWhiteSpace($Thumbprint)) {
    $Thumbprint = [Environment]::GetEnvironmentVariable('LIGHTHOST_MANIFEST_SIGNING_THUMBPRINT', 'Process')
}
if ([string]::IsNullOrWhiteSpace($Thumbprint)) {
    $Thumbprint = [Environment]::GetEnvironmentVariable('LIGHTHOST_MANIFEST_SIGNING_THUMBPRINT', 'User')
}
if ([string]::IsNullOrWhiteSpace($Thumbprint)) { throw 'Informe -Thumbprint ou configure LIGHTHOST_MANIFEST_SIGNING_THUMBPRINT no usuario.' }
$Thumbprint = ($Thumbprint -replace '\s', '').ToUpperInvariant()
if ($Thumbprint -notmatch '\A[0-9A-F]{40}\z') { throw 'Thumbprint invalido; esperado SHA-1 de 40 digitos hexadecimais.' }

# Hold directory handles without write/delete sharing, reject reparse points,
# and compare canonical paths (including aliases such as short DOS names).
if (-not ('LightHostModernSigningBackupV1.PathGuard' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
namespace LightHostModernSigningBackupV1 {
    public static class PathGuard {
        [StructLayout(LayoutKind.Sequential)] private struct AttributeTag {
            public uint Attributes;
            public uint Tag;
        }
        [StructLayout(LayoutKind.Sequential)] private struct SecurityAttributes {
            public int Length;
            public IntPtr Descriptor;
            [MarshalAs(UnmanagedType.Bool)] public bool InheritHandle;
        }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
            IntPtr security, uint creation, uint flags, IntPtr template);
        [DllImport("kernel32.dll", SetLastError = true, ExactSpelling = true)]
        private static extern bool GetFileInformationByHandleEx(SafeFileHandle file, int type,
            out AttributeTag information, uint size);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
        private static extern uint GetFinalPathNameByHandleW(SafeFileHandle file,
            StringBuilder path, uint capacity, uint flags);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
        private static extern bool CreateDirectoryW(string path, ref SecurityAttributes security);
        public static SafeFileHandle OpenDirectory(string path) {
            SafeFileHandle handle = CreateFileW(path, 0x80000000, 1, IntPtr.Zero, 3,
                0x02000000 | 0x00200000, IntPtr.Zero);
            if (handle.IsInvalid) { int error = Marshal.GetLastWin32Error(); handle.Dispose(); throw new Win32Exception(error); }
            try {
                AttributeTag tag;
                if (!GetFileInformationByHandleEx(handle, 9, out tag, 8)) throw new Win32Exception(Marshal.GetLastWin32Error());
                if ((tag.Attributes & 0x400) != 0 || (tag.Attributes & 0x10) == 0)
                    throw new InvalidOperationException("Reparse point ou diretorio invalido: " + path);
                return handle;
            } catch { handle.Dispose(); throw; }
        }
        public static string CanonicalPath(SafeFileHandle handle) {
            StringBuilder path = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandleW(handle, path, (uint)path.Capacity, 0);
            if (length == 0 || length >= path.Capacity) throw new Win32Exception(Marshal.GetLastWin32Error());
            string result = path.ToString();
            if (result.StartsWith(@"\\?\UNC\", StringComparison.OrdinalIgnoreCase)) return @"\\" + result.Substring(8);
            return result.StartsWith(@"\\?\", StringComparison.Ordinal) ? result.Substring(4) : result;
        }
        public static void CreatePrivateDirectory(string path, byte[] descriptor) {
            GCHandle pinned = GCHandle.Alloc(descriptor, GCHandleType.Pinned);
            try {
                SecurityAttributes security = new SecurityAttributes();
                security.Length = Marshal.SizeOf(typeof(SecurityAttributes));
                security.Descriptor = pinned.AddrOfPinnedObject();
                security.InheritHandle = false;
                // Unlike Directory.CreateDirectory, this fails if the path exists.
                if (!CreateDirectoryW(path, ref security)) throw new Win32Exception(Marshal.GetLastWin32Error());
            } finally { pinned.Free(); }
        }
    }
}
'@
}

$directoryLocks = [Collections.Generic.List[Microsoft.Win32.SafeHandles.SafeFileHandle]]::new()
function Lock-DirectoryChain([string] $Directory) {
    $chain = [Collections.Generic.Stack[string]]::new()
    $current = [IO.DirectoryInfo]::new($Directory)
    while ($null -ne $current) { $chain.Push($current.FullName); $current = $current.Parent }
    $last = $null
    foreach ($path in $chain) {
        $last = [LightHostModernSigningBackupV1.PathGuard]::OpenDirectory($path)
        $directoryLocks.Add($last)
    }
    $canonical = [LightHostModernSigningBackupV1.PathGuard]::CanonicalPath($last)
    if ($canonical.Equals([IO.Path]::GetPathRoot($canonical), [StringComparison]::OrdinalIgnoreCase)) { return $canonical }
    return $canonical.TrimEnd('\')
}

$password = $null
$confirmation = $null
$certificate = $null
$publicRsa = $null
$pfxData = $null
$stageDirectory = $null
$stageFile = $null
$stageLock = $null
$stageCreated = $false
$stageFileOwned = $false
$destinationOwned = $false
$destination = $null
$published = $false
try {
    $null = Get-Command Export-PfxCertificate, Get-PfxData -ErrorAction Stop
    $certificate = Get-Item -LiteralPath ('Cert:\CurrentUser\My\' + $Thumbprint) -ErrorAction Stop
    if ($certificate.Subject -cne 'CN=LightHostModern Update Manifest Signing' -or
        $certificate.FriendlyName -cne 'LightHostModern Update Manifest Signing' -or -not $certificate.HasPrivateKey) {
        throw 'Este certificado nao e a identidade dedicada de assinatura do updater LightHostModern com chave privada.'
    }
    $publicRsa = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($certificate)
    if ($null -eq $publicRsa -or $publicRsa.KeySize -lt 3072) { throw 'A identidade dedicada deve usar RSA de pelo menos 3072 bits.' }

    Write-Host ('Backup da identidade dedicada: ' + $Thumbprint)
    $requestedPath = Read-Host 'Caminho completo do NOVO arquivo .pfx fora do repositorio (Enter cancela)'
    if ([string]::IsNullOrWhiteSpace($requestedPath)) { throw 'Backup cancelado: destino vazio.' }
    $requestedPath = $requestedPath.Trim().Replace('/', '\')
    if ($requestedPath -notmatch '\A[A-Za-z]:\\' -or $requestedPath.Substring(2).Contains(':')) {
        throw 'Use um caminho absoluto de unidade Windows, sem caminho de dispositivo, UNC ou fluxo alternativo.'
    }
    foreach ($part in $requestedPath.Substring(3).Split('\')) {
        if ($part.EndsWith('.') -or $part.EndsWith(' ')) { throw 'Componentes com ponto ou espaco final nao sao permitidos.' }
    }
    $fullPath = [IO.Path]::GetFullPath($requestedPath)
    $fileName = [IO.Path]::GetFileName($fullPath)
    if ([IO.Path]::GetExtension($fileName) -ine '.pfx' -or
        [string]::IsNullOrWhiteSpace([IO.Path]::GetFileNameWithoutExtension($fileName)) -or
        $fileName.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) { throw 'Escolha um nome valido terminado em .pfx.' }

    $repo = Lock-DirectoryChain ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')))
    $parent = Lock-DirectoryChain ([IO.Path]::GetDirectoryName($fullPath))
    $destination = [IO.Path]::Combine($parent, $fileName)
    if ($destination.Equals($repo, [StringComparison]::OrdinalIgnoreCase) -or
        $destination.StartsWith($repo + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'O backup da chave privada nao pode ser gravado dentro do repositorio.'
    }
    if (Test-Path -LiteralPath $destination) { throw 'O destino ja existe. Nenhum arquivo sera sobrescrito.' }

    $password = Read-Host 'Senha forte e exclusiva para proteger o PFX (vazia cancela)' -AsSecureString
    if ($null -eq $password -or $password.Length -eq 0) { throw 'Backup cancelado: senha vazia.' }
    $confirmation = Read-Host 'Repita a senha do PFX' -AsSecureString
    if ($null -eq $confirmation -or $confirmation.Length -eq 0 -or $confirmation.Length -ne $password.Length) {
        throw 'Backup cancelado: a confirmacao nao corresponde a senha.'
    }
    $password.MakeReadOnly()
    $confirmation.MakeReadOnly()

    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $directorySecurity = [Security.AccessControl.DirectorySecurity]::new()
    $directorySecurity.SetOwner($sid)
    $directorySecurity.SetAccessRuleProtection($true, $false)
    $inheritance = [Security.AccessControl.InheritanceFlags]::ContainerInherit -bor [Security.AccessControl.InheritanceFlags]::ObjectInherit
    $directorySecurity.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($sid,
        [Security.AccessControl.FileSystemRights]::FullControl, $inheritance,
        [Security.AccessControl.PropagationFlags]::None, [Security.AccessControl.AccessControlType]::Allow))
    $stageDirectory = [IO.Path]::Combine($parent, '.LightHostModern-key-backup-' + [Guid]::NewGuid().ToString('N'))
    [LightHostModernSigningBackupV1.PathGuard]::CreatePrivateDirectory($stageDirectory, $directorySecurity.GetSecurityDescriptorBinaryForm())
    $stageCreated = $true
    $stageLock = [LightHostModernSigningBackupV1.PathGuard]::OpenDirectory($stageDirectory)
    $actualSecurity = [IO.Directory]::GetAccessControl($stageDirectory)
    $rules = $actualSecurity.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier])
    if (-not $actualSecurity.AreAccessRulesProtected -or $rules.Count -ne 1 -or
        $rules[0].IdentityReference.Value -ne $sid.Value -or
        $rules[0].AccessControlType -ne [Security.AccessControl.AccessControlType]::Allow -or
        $rules[0].FileSystemRights -ne [Security.AccessControl.FileSystemRights]::FullControl) {
        throw 'O destino nao preservou a ACL exclusiva do usuario; nenhum PFX sera exportado.'
    }
    $stageFile = [IO.Path]::Combine($stageDirectory, 'encrypted-backup.pfx')
    if (Test-Path -LiteralPath $stageFile) { throw 'O staging inesperadamente ja possui um arquivo; backup cancelado.' }
    $stageFileOwned = $true
    try {
        $null = Export-PfxCertificate -Cert $certificate -FilePath $stageFile -Password $password `
            -CryptoAlgorithmOption AES256_SHA256 -ChainOption EndEntityCertOnly -NoClobber -ErrorAction Stop
    } catch { throw 'Falha ao exportar o PFX protegido. Verifique suporte do Windows e permissao de exportacao da chave; nenhum destino foi publicado.' }

    # Password confirmation is cryptographic: no SecureString is converted to
    # plaintext, BSTR, a managed string, an environment variable or a log entry.
    try { $pfxData = Get-PfxData -FilePath $stageFile -Password $confirmation -ErrorAction Stop }
    catch { throw 'A confirmacao nao abriu o PFX: senhas diferentes ou backup invalido. Nenhum destino foi publicado.' }
    $endCertificates = @($pfxData.EndEntityCertificates)
    if ($endCertificates.Count -ne 1 -or $endCertificates[0].Thumbprint -ine $Thumbprint) {
        throw 'O PFX verificado nao corresponde somente ao certificado selecionado; backup cancelado.'
    }
    $fileSecurity = [Security.AccessControl.FileSecurity]::new()
    $fileSecurity.SetOwner($sid)
    $fileSecurity.SetAccessRuleProtection($true, $false)
    $fileSecurity.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($sid,
        [Security.AccessControl.FileSystemRights]::FullControl, [Security.AccessControl.AccessControlType]::Allow))
    [IO.File]::SetAccessControl($stageFile, $fileSecurity)
    # Keep ancestor locks held: renaming into a locked parent is rejected by
    # Windows. CreateNew refuses collisions, applies the private ACL at creation,
    # and denies readers until the already-verified encrypted bytes are flushed.
    $backupInput = [IO.File]::Open($stageFile, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        $backupOutput = [IO.FileStream]::new($destination, [IO.FileMode]::CreateNew,
            [Security.AccessControl.FileSystemRights]::FullControl, [IO.FileShare]::None,
            4096, [IO.FileOptions]::WriteThrough, $fileSecurity)
        $destinationOwned = $true
        try { $backupInput.CopyTo($backupOutput); $backupOutput.Flush($true) }
        finally { $backupOutput.Dispose() }
    } finally { $backupInput.Dispose() }
    $published = $true
    Write-Host ('Backup PFX criptografado e senha de confirmacao verificados: ' + $destination)
    Write-Host 'Guarde o PFX fora desta maquina e a senha em local separado. Nao envie a senha ao chat.'
} finally {
    if ($null -ne $pfxData) {
        foreach ($cert in @($pfxData.EndEntityCertificates) + @($pfxData.OtherCertificates)) {
            if ($null -ne $cert) { $cert.Dispose() }
        }
    }
    if ($null -ne $publicRsa) { $publicRsa.Dispose() }
    if ($null -ne $certificate) { $certificate.Dispose() }
    if ($null -ne $password) { $password.Dispose() }
    if ($null -ne $confirmation) { $confirmation.Dispose() }
    if ($destinationOwned -and -not $published) {
        try {
            if (([IO.File]::GetAttributes($destination) -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Destino mudou para reparse point.' }
            [IO.File]::Delete($destination)
        } catch { Write-Warning ('Nao foi possivel remover o backup incompleto criado nesta execucao: ' + $destination) }
    }
    if ($stageCreated) {
        try {
            if ($null -eq $stageLock) { $stageLock = [LightHostModernSigningBackupV1.PathGuard]::OpenDirectory($stageDirectory) }
            $verifiedStage = [LightHostModernSigningBackupV1.PathGuard]::CanonicalPath($stageLock)
            if (-not $verifiedStage.Equals($stageDirectory, [StringComparison]::OrdinalIgnoreCase)) { throw 'Identidade do staging mudou.' }
            if ($stageFileOwned -and [IO.File]::Exists($stageFile)) {
                if (([IO.File]::GetAttributes($stageFile) -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Staging contem reparse point.' }
                [IO.File]::Delete($stageFile)
            }
            $stageLock.Dispose()
            $stageLock = $null
            [IO.Directory]::Delete($stageDirectory, $false) # Empty own directory only; never recursive.
        } catch { Write-Warning ('Nao foi possivel remover o staging protegido em ' + $stageDirectory + '. Verifique esse local; nenhum outro caminho foi removido.') }
    }
    if ($null -ne $stageLock) { $stageLock.Dispose() }
    for ($index = $directoryLocks.Count - 1; $index -ge 0; --$index) { $directoryLocks[$index].Dispose() }
}
