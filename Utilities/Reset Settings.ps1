[CmdletBinding()]
param([string]$SettingsDirectory = (Join-Path $env:APPDATA 'LightHostModern'))

$ErrorActionPreference = 'Stop'

# The host owns the reset inventory, retries and migration tombstone. This helper
# only requests that reset; deleting preferences here can revive recovery copies.
function Assert-UnlinkedPath([string]$Path) {
    for ($at = $Path; $at; $at = [IO.Path]::GetDirectoryName($at)) {
        try {
            $attributes = [IO.File]::GetAttributes($at)
            if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Settings reset refuses linked paths: $at"
            }
        } catch [IO.FileNotFoundException] {
        } catch [IO.DirectoryNotFoundException] {
        }
    }
}

$directory = [IO.Path]::GetFullPath($SettingsDirectory)
$marker = Join-Path $directory 'LightHostModern.settings.factory-reset'
Assert-UnlinkedPath $marker
[IO.Directory]::CreateDirectory($directory) | Out-Null
Assert-UnlinkedPath $marker

if ([IO.File]::Exists($marker)) { return }

# The marker's existence is the request; CreateNew also refuses a preexisting
# link. A partial write remains a valid request, so no replacement is needed.
$stream = [IO.FileStream]::new($marker, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
try {
    $bytes = [Text.Encoding]::ASCII.GetBytes("reset`n")
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Flush($true)
} finally {
    $stream.Dispose()
}
