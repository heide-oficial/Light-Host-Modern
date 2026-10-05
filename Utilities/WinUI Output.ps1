param(
    [ValidateSet('Resolve', 'Record', 'Stage')][string] $Mode = 'Resolve',
    [ValidateSet('x64', 'ARM64', 'Win32')][string] $Platform = 'x64',
    [ValidateSet('Debug', 'Release')][string] $Configuration = 'Release',
    [string] $Destination = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$propsPath = Join-Path $repoRoot 'WinUI\LightHostModern.Output.props'
[xml] $props = Get-Content -LiteralPath $propsPath -Raw
$relative = [string] $props.Project.PropertyGroup.OutDir
$source = [IO.Path]::GetFullPath($relative.Replace('$(MSBuildThisFileDirectory)', (Join-Path $repoRoot 'WinUI\')).Replace('$(Platform)', $Platform).Replace('$(Configuration)', $Configuration))
if ($Mode -eq 'Resolve') { return $source }

function Get-SourceFingerprint {
    $project = Join-Path $repoRoot 'WinUI\LightHostModern.WinUI'
    $files = @(Get-ChildItem -LiteralPath $project -File | Where-Object { $_.Extension -in '.h', '.cpp', '.idl', '.xaml', '.vcxproj', '.config', '.manifest', '.appxmanifest', '.rc' })
    foreach ($child in 'Locales', 'Assets') { $files += Get-ChildItem -LiteralPath (Join-Path $project $child) -File -Recurse }
    $files += Get-Item -LiteralPath $propsPath
    $files += Get-Item -LiteralPath (Join-Path $repoRoot 'WinUI\LightHostModern.WinUI.sln')
    $files += Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Source') -File -Filter '*.h'
    $lines = foreach ($file in ($files | Sort-Object FullName -Unique)) {
        $file.FullName.Substring($repoRoot.Length).Replace('\', '/') + ':' + (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Assert-WorkspacePath([string] $Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    $allowed = [IO.Path]::GetFullPath((Join-Path $repoRoot 'out')) + '\'
    if (!$resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw "Output must be inside $allowed" }
    for ($ancestor = $resolved; $ancestor.Length -gt $repoRoot.Length; $ancestor = [IO.Path]::GetDirectoryName($ancestor)) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Reparse point in output path: $ancestor" }
    }
    return $resolved
}
$exe = Join-Path $source 'LightHostModernWinUI.exe'
$stampPath = Join-Path $source 'lighthost-build.json'
if (!(Test-Path -LiteralPath $exe)) { throw "Canonical WinUI executable was not found: $exe" }
$fingerprint = Get-SourceFingerprint
if ($Mode -eq 'Record') {
    [ordered]@{ format = 1; sourceHash = $fingerprint; exeHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash; configuration = $Configuration; platform = $Platform } |
        ConvertTo-Json | Set-Content -LiteralPath $stampPath -Encoding UTF8
    return
}
if (!(Test-Path -LiteralPath $stampPath)) { throw 'Build the WinUI solution before staging: its successful-build record is missing.' }
$stamp = Get-Content -LiteralPath $stampPath -Raw | ConvertFrom-Json
if ($stamp.sourceHash -ne $fingerprint -or $stamp.exeHash -ne (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -or $stamp.configuration -ne $Configuration -or $stamp.platform -ne $Platform) {
    throw 'WinUI output does not match current sources. Rebuild the WinUI solution before staging.'
}
$target = Assert-WorkspacePath $Destination
if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
New-Item -ItemType Directory -Path $target -Force | Out-Null
foreach ($item in Get-ChildItem -LiteralPath $source) {
    if ($item.Name -in 'obj', 'AppX', 'lighthost-build.json' -or $item.Extension -in '.pdb', '.ilk', '.exp', '.lib', '.appxsym', '.recipe', '.appxrecipe', '.map', '.winmd', '.xml') { continue }
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point in WinUI output: $($item.FullName)" }
    Copy-Item -LiteralPath $item.FullName -Destination $target -Recurse -Force
}
if ((Get-FileHash -LiteralPath (Join-Path $target 'LightHostModernWinUI.exe') -Algorithm SHA256).Hash -ne $stamp.exeHash) { throw 'Staged WinUI executable hash changed.' }
