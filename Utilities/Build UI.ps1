param(
    [ValidateSet('Release', 'Debug')][string] $Configuration = 'Release',
    [ValidateSet('x64')][string] $Platform = 'x64'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Build Environment.ps1')
$msbuild = Get-ProjectMSBuild
Restore-ProjectNuGet -MSBuildPath $msbuild
$arguments = @((Join-Path $projectRoot 'WinUI\LightHostModern.WinUI\LightHostModern.WinUI.vcxproj'),
    '/m:4', '/v:minimal', "/p:Configuration=$Configuration", "/p:Platform=$Platform")
$arguments += @(Get-LocalNuGetArguments)
& $msbuild @arguments
if ($LASTEXITCODE -ne 0) { throw "WinUI build failed ($LASTEXITCODE)." }
