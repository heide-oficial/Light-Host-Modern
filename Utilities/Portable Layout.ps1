# Shared portable packaging contract. MSI intentionally keeps its flat layout.
function New-VersionedPortable([string] $Source, [string] $Destination, [string] $Launcher) {
    $sourcePath = [IO.Path]::GetFullPath($Source).TrimEnd('\')
    $destinationPath = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
    $workspacePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\') + '\'
    foreach ($path in @($sourcePath, $destinationPath, [IO.Path]::GetFullPath($Launcher))) {
        if (!$path.StartsWith($workspacePath, [StringComparison]::OrdinalIgnoreCase)) { throw "Packaging path outside workspace: $path" }
        for ($parent = $path; $parent.Length -gt $workspacePath.Length; $parent = [IO.Path]::GetDirectoryName($parent)) {
            if ((Test-Path -LiteralPath $parent) -and ((Get-Item -LiteralPath $parent).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Reparse point: $parent" }
        }
    }
    if (Test-Path -LiteralPath $destinationPath) { throw 'Portable destination must be new.' }
    $info = Get-Content -LiteralPath (Join-Path $sourcePath 'release-info.json') -Raw | ConvertFrom-Json
    $files = @(Get-ChildItem -LiteralPath $sourcePath -Recurse -File | Sort-Object FullName | ForEach-Object {
        if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse points are not allowed in payloads.' }
        [ordered]@{ path = $_.FullName.Substring($sourcePath.Length + 1).Replace('\','/'); size = $_.Length;
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    $manifest = [ordered]@{ formatVersion = 1; version = $info.version; platform = 'x64'; files = $files } | ConvertTo-Json -Depth 6
    $utf8 = New-Object Text.UTF8Encoding($false)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = ([BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes($manifest)))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    $id = $info.version + '-' + $hash.Substring(0,24)
    $payload = Join-Path $destinationPath ('versions\' + $id)
    New-Item -ItemType Directory -Path $payload -Force | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $sourcePath) { Copy-Item -LiteralPath $file.FullName -Destination $payload -Recurse }
    [IO.File]::WriteAllText((Join-Path $payload 'payload-manifest.json'), $manifest, $utf8)
    Copy-Item -LiteralPath $Launcher -Destination (Join-Path $destinationPath 'LightHostModern.exe')
    foreach ($name in 'README.md','LICENSE','THIRD-PARTY-NOTICES.txt','Licenses','release-info.json') {
        if (Test-Path -LiteralPath (Join-Path $sourcePath $name)) { Copy-Item -LiteralPath (Join-Path $sourcePath $name) -Destination $destinationPath -Recurse }
    }
    $layout = [ordered]@{ formatVersion = 1; launcherVersion = 1; initial = [ordered]@{ id = $id; inventoryHash = $hash } }
    [IO.File]::WriteAllText((Join-Path $destinationPath 'portable-layout.json'), ($layout | ConvertTo-Json -Depth 4), $utf8)
    return $layout
}
