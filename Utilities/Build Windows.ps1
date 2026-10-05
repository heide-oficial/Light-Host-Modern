param(
    [ValidateSet("Debug", "Release")]
    [string] $Configuration = "Release",

    [string] $Preset = "windows-vs2022",

    [ValidateSet("AUTO", "ON", "OFF")]
    [string] $EnableVst2 = "AUTO",

[ValidateSet("LEGACY", "XAYMAR")]
[string] $Vst2Provider = "XAYMAR",

    [string] $Vst2SdkDir = "",

    [switch] $ApplicationOnly
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot 'Build Environment.ps1')
$cmakePath = Get-ProjectCMake

if (!(Test-Path $cmakePath)) {
    throw "CMake 3.22+ was not found. Install current CMake and Visual Studio Build Tools 2022."
}

$configureArgs = @(
    "--preset", $Preset,
    "-DLIGHTHOST_ENABLE_VST2=$EnableVst2",
    "-DLIGHTHOST_VST2_PROVIDER=$Vst2Provider"
)
$configureArgs += @(Get-LocalDependencyArguments)

if (![string]::IsNullOrWhiteSpace($Vst2SdkDir)) {
    $configureArgs += "-DLIGHTHOST_VST2_SDK_DIR=$Vst2SdkDir"
} elseif (![string]::IsNullOrWhiteSpace($env:LIGHTHOST_VST2_SDK_DIR)) {
    $configureArgs += "-DLIGHTHOST_VST2_SDK_DIR=$env:LIGHTHOST_VST2_SDK_DIR"
}

& $cmakePath @configureArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

# Compile the UI before the host stages it. Both entry points resolve to the
# same output through LightHostModern.Output.props and validate its source stamp.
& (Join-Path $PSScriptRoot 'Build UI.ps1') -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$buildArgs = @('--build', '--preset', "$Preset-$($Configuration.ToLowerInvariant())")
if ($ApplicationOnly) { $buildArgs += @('--target', 'LightHostModern') }
& $cmakePath @buildArgs
exit $LASTEXITCODE
