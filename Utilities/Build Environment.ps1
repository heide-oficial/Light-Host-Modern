# Shared local tool/dependency resolution. Build products never serve as caches.
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Get-ProjectCMake {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in @((Join-Path $projectRoot 'out\tools\python\cmake\data\bin\cmake.exe'),
                              (Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'))) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw 'CMake 3.22+ was not found. Install CMake or restore the local out/tools cache.'
}

function Get-ProjectMSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer (vswhere) was not found.' }
    $candidate = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    if (!$candidate) { throw 'MSBuild with C++ and WinUI tooling was not found.' }
    return $candidate
}

function Get-LocalDependencyArguments {
    # Versioned source-only caches. Missing caches use CMake's pinned downloads.
    $entries = @(
        @('LIGHTHOST_JUCE_DIR', 'juce-8.0.13', 'CMakeLists.txt'),
        @('LIGHTHOST_VST3_SDK_DIR', 'vst3sdk-3.8.0', 'CMakeLists.txt'),
        @('LIGHTHOST_ASIO_SDK_DIR', 'asio-sdk', 'common\asio.h'),
        @('FETCHCONTENT_SOURCE_DIR_XAYMAR_VST2SDK', 'xaymar-vst2sdk-0.4.0', 'include\vst.h')
    )
    foreach ($entry in $entries) {
        $directory = Join-Path $projectRoot ('out\deps\' + $entry[1])
        if (Test-Path -LiteralPath (Join-Path $directory $entry[2])) {
            '-D' + $entry[0] + '=' + $directory.Replace('\', '/')
        }
    }
}

function Get-LocalNuGetArguments {
    $root = Join-Path $projectRoot 'out\nuget'
    if (Test-Path -LiteralPath $root -PathType Container) { '/p:NuGetPackageRoot=' + $root + '\' }
}

function Restore-ProjectNuGet {
    param([string]$MSBuildPath = '', [string]$CacheRoot = '')
    if (!$MSBuildPath) { $MSBuildPath = Get-ProjectMSBuild }
    if (!$CacheRoot) { $CacheRoot = Join-Path $projectRoot 'out\nuget' }
    $CacheRoot = [IO.Path]::GetFullPath($CacheRoot).TrimEnd('\')
    $allowed = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out')).TrimEnd('\') + '\'
    if (!$CacheRoot.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'NuGet cache must be below the workspace out directory.' }
    for ($cursor = $CacheRoot; $cursor; $cursor = [IO.Path]::GetDirectoryName($cursor)) {
        if ((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'NuGet cache cannot contain links or reparse points.'
        }
    }
    [xml]$configuration = Get-Content -LiteralPath (Join-Path $projectRoot 'WinUI\LightHostModern.WinUI\packages.config') -Raw
    $downloads = [Collections.Generic.List[string]]::new()
    $required = [Collections.Generic.List[string]]::new()
    $seen = @{}
    foreach ($package in $configuration.packages.package) {
        $id = [string]$package.id; $version = [string]$package.version
        if ($id -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]*$' -or $seen.ContainsKey($id) -or
            $version -notmatch '^[0-9]+(?:\.[0-9]+){1,3}(?:-[A-Za-z0-9.-]+)?$') { throw 'Invalid or duplicated pinned NuGet package.' }
        $seen[$id] = $true
        $lower = $id.ToLowerInvariant()
        $required.Add((Join-Path $CacheRoot "$lower\$version\$lower.nuspec"))
        $downloads.Add('<PackageDownload Include="' + $id + '" Version="[' + $version + ']" />')
    }
    if (!$downloads.Count) { throw 'No pinned NuGet packages were found.' }
    [xml]$project = Get-Content -LiteralPath (Join-Path $projectRoot 'WinUI\LightHostModern.WinUI\LightHostModern.WinUI.vcxproj') -Raw
    foreach ($import in $project.Project.Import) {
        $path = [string]$import.Project
        if ($path.StartsWith('$(NuGetPackageRoot)', [StringComparison]::Ordinal)) {
            $relative = $path.Substring('$(NuGetPackageRoot)'.Length)
            $resolved = [IO.Path]::GetFullPath((Join-Path $CacheRoot $relative))
            if (!$resolved.StartsWith($CacheRoot+'\', [StringComparison]::OrdinalIgnoreCase)) { throw 'NuGet import escaped the cache.' }
            $required.Add($resolved)
        }
    }
    $missing = @($required | Where-Object { !(Test-Path -LiteralPath $_ -PathType Leaf) })
    if (!$missing.Count) { return }

    # A plain /restore of packages.config is not enabled by default and its
    # ID.version layout differs from the ID/version imports above. Download the
    # complete pinned list through NuGet's global-packages layout instead. This
    # standalone restore project has no compiler targets and needs no .NET SDK.
    $nugetTargets = ''
    for ($cursor = [IO.Path]::GetDirectoryName($MSBuildPath); $cursor; $cursor = [IO.Path]::GetDirectoryName($cursor)) {
        $candidate = Join-Path $cursor 'Common7\IDE\CommonExtensions\Microsoft\NuGet\NuGet.targets'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $nugetTargets = $candidate; break }
    }
    if (!$nugetTargets) { throw 'Visual Studio NuGet build targets were not found. Install the NuGet package manager component.' }
    $work = Join-Path $CacheRoot ('.restore-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work -Force | Out-Null
    $restoreProject = Join-Path $work 'restore.proj'
    $xml = '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><PropertyGroup>' +
        '<RestoreProjectStyle>PackageReference</RestoreProjectStyle><TargetFramework>net472</TargetFramework>' +
        '<TargetFrameworkIdentifier>.NETFramework</TargetFrameworkIdentifier><TargetFrameworkVersion>v4.7.2</TargetFrameworkVersion>' +
        '<TargetFrameworkMoniker>.NETFramework,Version=v4.7.2</TargetFrameworkMoniker>' +
        '<RestoreOutputPath>' + [Security.SecurityElement]::Escape((Join-Path $work 'obj')) + '</RestoreOutputPath>' +
        '</PropertyGroup><ItemGroup>' + ($downloads -join '') + '</ItemGroup><Import Project="' +
        [Security.SecurityElement]::Escape($nugetTargets) + '" /></Project>'
    [IO.File]::WriteAllText($restoreProject, $xml)
    Write-Host 'Restoring the exact WinUI packages into the workspace NuGet cache...'
    & $MSBuildPath $restoreProject /t:Restore /nologo /v:minimal "/p:RestorePackagesPath=$CacheRoot" /p:RestoreSources=https://api.nuget.org/v3/index.json | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Pinned NuGet restore failed ($LASTEXITCODE). Details: $work" }
    $missing = @($required | Where-Object { !(Test-Path -LiteralPath $_ -PathType Leaf) })
    if ($missing.Count) { throw ('NuGet restore left required files missing: ' + ($missing -join ', ')) }
}
