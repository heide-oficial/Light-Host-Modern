param(
    [string] $Preset = 'windows-vs2022',
    [switch] $SkipBuild,
    # Only for adopting an existing, current build. Never a previous portable.
    [string] $HostOutput = ''
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Build Environment.ps1')
. (Join-Path $PSScriptRoot 'Portable Layout.ps1')

function Assert-LocalDirectory([string] $Path, [string] $Root) {
    $resolved = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $allowed = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    if (!$resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw "Path outside $allowed : $resolved" }
    for ($parent = $resolved; $parent.Length -gt $projectRoot.Length; $parent = [IO.Path]::GetDirectoryName($parent)) {
        if ((Test-Path -LiteralPath $parent) -and ((Get-Item -LiteralPath $parent).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Reparse point in output path: $parent"
        }
    }
    return $resolved
}

if (!$SkipBuild) {
    if ($HostOutput) { throw 'HostOutput can only be used together with SkipBuild.' }
    & (Join-Path $PSScriptRoot 'Build Windows.ps1') -Preset $Preset -Configuration Release -ApplicationOnly
    if ($LASTEXITCODE -ne 0) { throw "Native build failed ($LASTEXITCODE)." }
}
if (!$HostOutput) { $HostOutput = Join-Path $projectRoot "out\build\$Preset\LightHostModern_artefacts\Release" }
$HostOutput = Assert-LocalDirectory $HostOutput (Join-Path $projectRoot 'out')
$versionMatch = [regex]::Match([IO.File]::ReadAllText((Join-Path $projectRoot 'CMakeLists.txt')), 'project\(LightHostModern VERSION ([0-9]+\.[0-9]+\.[0-9]+)')
if (!$versionMatch.Success) { throw 'App version was not found in CMakeLists.txt.' }
$version = $versionMatch.Groups[1].Value
$work = Assert-LocalDirectory (Join-Path $projectRoot ('out\dev-package\' + [guid]::NewGuid().ToString('N'))) (Join-Path $projectRoot 'out')
$payload = Join-Path $work 'portable'
New-Item -ItemType Directory -Path $payload -Force | Out-Null
$lock = $null
try {
    foreach ($name in 'LightHostModern.exe', 'LightHostModernScanner.exe', 'LightHostModernWorker.exe', 'LightHostModernUpdateHelper.exe') {
        $source = Get-Item -LiteralPath (Join-Path $HostOutput $name)
        if ($source.VersionInfo.FileVersion -ne $version -or $source.VersionInfo.ProductVersion -ne $version) {
            throw "Rebuild before packaging: $name does not match $version."
        }
        Copy-Item -LiteralPath $source.FullName -Destination $payload
        if ((Get-FileHash -LiteralPath $source.FullName).Hash -ne (Get-FileHash -LiteralPath (Join-Path $payload $name)).Hash) {
            throw "Copy verification failed: $name"
        }
    }
    $uiDestination = Join-Path $payload 'WinUI\x64\Release\LightHostModern.WinUI'
    & (Join-Path $PSScriptRoot 'WinUI Output.ps1') -Mode Stage -Destination $uiDestination
    $uiExe = Join-Path $uiDestination 'LightHostModernWinUI.exe'
    if ((Get-Item -LiteralPath $uiExe).VersionInfo.FileVersion -ne $version) { throw 'WinUI version differs from host.' }
    Copy-Item -LiteralPath (Join-Path $projectRoot 'license') -Destination (Join-Path $payload 'LICENSE')
    foreach ($name in 'README.md', 'THIRD-PARTY-NOTICES.txt') { Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $payload }
    Copy-Item -LiteralPath (Join-Path $projectRoot 'ThirdParty\Licenses') -Destination (Join-Path $payload 'Licenses') -Recurse
    $redistRoots = @('C:\Program Files\Microsoft Visual Studio', 'C:\Program Files (x86)\Microsoft Visual Studio')
    $crt = @($redistRoots | Where-Object { Test-Path -LiteralPath $_ } | ForEach-Object {
        Get-ChildItem -LiteralPath $_ -Filter 'Microsoft.VC*.CRT' -Directory -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\Redist\\MSVC\\[^\\]+\\x64\\Microsoft\.VC\d+\.CRT$' }
    }) | Sort-Object FullName -Descending | Select-Object -First 1
    if (!$crt) { throw 'The x64 VC runtime was not found. Install the Visual C++ build tools.' }
    Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll' -File | Copy-Item -Destination $payload

    $devRoot = Join-Path $projectRoot 'dev-test'
    New-Item -ItemType Directory -Path $devRoot -Force | Out-Null
    # Serialize publication, not compilation. Keep numbering after old manual
    # builds are deleted; never overwrite an existing or running portable.
    $lock = [IO.File]::Open((Join-Path $devRoot '.build.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $counterFile = Join-Path $devRoot '.build-number'
    $last = 0L
    if (Test-Path -LiteralPath $counterFile) {
        if (![long]::TryParse([IO.File]::ReadAllText($counterFile).Trim(), [ref]$last) -or $last -lt 0) { throw 'Invalid dev build counter.' }
    }
    foreach ($folder in Get-ChildItem -LiteralPath $devRoot -Directory) {
        if ($folder.Name -match '^LightHostModern-build-(\d+)$') { $last = [Math]::Max($last, [long]$Matches[1]) }
    }
    $number = $last + 1
    $name = 'LightHostModern-build-{0:D4}' -f $number
    $destination = Assert-LocalDirectory (Join-Path $devRoot $name) $devRoot
    [ordered]@{ name = 'LightHostModern'; version = $version; developmentBuild = $number;
        configuration = 'Release'; platform = 'x64'; builtAtUtc = (Get-Date).ToUniversalTime().ToString('o');
        entryPoint = 'LightHostModern.exe'; uiSha256 = (Get-FileHash -LiteralPath $uiExe -Algorithm SHA256).Hash
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $payload 'release-info.json') -Encoding UTF8
    $versioned = Join-Path $work 'versioned-portable'
    $null = New-VersionedPortable $payload $versioned (Join-Path $HostOutput 'LightHostModernLauncher.exe')
    [IO.Directory]::Move($versioned, $destination)
    [IO.File]::WriteAllText($counterFile, [string]$number)
    Write-Host "Development portable: $destination"
} finally {
    if ($lock) { $lock.Dispose() }
    $checked = Assert-LocalDirectory $work (Join-Path $projectRoot 'out')
    if (Test-Path -LiteralPath $checked) { Remove-Item -LiteralPath $checked -Recurse -Force }
}
