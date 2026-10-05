param(
    [ValidateSet("Release", "Debug")]
    [string] $Configuration = "Release",

    [ValidateSet("x64")]
    [string] $Platform = "x64",

    [string] $Preset = "windows-vs2022",

    [ValidateSet("AUTO", "ON", "OFF")]
    [string] $EnableVst2 = "AUTO",

    [ValidateSet("LEGACY", "XAYMAR")]
    [string] $Vst2Provider = "XAYMAR",

    [string] $Vst2SdkDir = "",

    [switch] $SkipBuild,

    [switch] $SkipTests,

    [switch] $KeepStage,

    [string] $OutputDirectory = "",

    [string] $SigningThumbprint = $env:LIGHTHOST_SIGNING_THUMBPRINT,

    [string] $SigningTimestampUrl = "http://timestamp.digicert.com",
    [string] $ManifestSigningThumbprint = $env:LIGHTHOST_MANIFEST_SIGNING_THUMBPRINT
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot 'Build Environment.ps1')
. (Join-Path $PSScriptRoot 'Portable Layout.ps1')
. (Join-Path $PSScriptRoot 'Update Signing.ps1')

if ([string]::IsNullOrWhiteSpace($ManifestSigningThumbprint)) {
    $ManifestSigningThumbprint = Get-ConfiguredManifestSigningThumbprint
}
if (![string]::IsNullOrWhiteSpace($ManifestSigningThumbprint)) {
    Assert-UpdateSigningTrust $ManifestSigningThumbprint (Join-Path $PSScriptRoot '..\Source\UpdateTrustKeys.h')
}

$appName = "LightHostModern"
$appVersion = "2.0.0"
$exeName = "LightHostModern.exe"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$outRoot = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $repoRoot "releases\v$appVersion" }
$packageWorkRoot = Join-Path $repoRoot ("out\package-work\v$appVersion-" + [guid]::NewGuid().ToString('N'))
$stageRoot = Join-Path $packageWorkRoot "payload"
$installerMsi = Join-Path $outRoot "LightHostModern-$appVersion-Setup.msi"
$portableZip = Join-Path $outRoot "LightHostModern-v$appVersion-Portable.zip"
$releaseIcon = Join-Path $repoRoot "Icon\logo.ico"

function Resolve-CMake {
    return Get-ProjectCMake
}

function Resolve-MSBuild {
    $candidates = @()
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

    if (Test-Path -LiteralPath $vswhere) {
        $candidates += & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe"
    }

    $candidates += @(
        "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    )

    foreach ($candidate in $candidates | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "MSBuild was not found. Install Visual Studio 2022/2026 with Desktop development with C++ and Windows App SDK tooling."
}

function Resolve-SignTool {
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $kitsBin = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    if (Test-Path -LiteralPath $kitsBin) {
        $candidate = Get-ChildItem -LiteralPath $kitsBin -Recurse -Filter signtool.exe -File -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\x64\\signtool\.exe$" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($null -ne $candidate) {
            return $candidate.FullName
        }
    }

    throw "signtool.exe was not found. Install the Windows SDK signing tools."
}

function Sign-ReleaseFile {
    param([Parameter(Mandatory)][string] $Path)

    if ([string]::IsNullOrWhiteSpace($SigningThumbprint)) {
        return
    }

    $signTool = Resolve-SignTool
    Invoke-Checked -FilePath $signTool -Arguments @(
        "sign", "/sha1", $SigningThumbprint, "/fd", "SHA256",
        "/tr", $SigningTimestampUrl, "/td", "SHA256", $Path
    )

    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        throw "The generated signature is not valid for '$Path': $($signature.StatusMessage)"
    }
}

function Resolve-VCVars64 {
    $candidateRoots = @(
        "C:\Program Files\Microsoft Visual Studio\18\Community",
        "C:\Program Files\Microsoft Visual Studio\18\Professional",
        "C:\Program Files\Microsoft Visual Studio\18\Enterprise",
        "C:\Program Files\Microsoft Visual Studio\2022\Community",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
    )

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installations = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $candidateRoots = @($installations) + $candidateRoots
    }

    foreach ($root in $candidateRoots | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique) {
        $vcvars = Join-Path $root "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path -LiteralPath $vcvars) {
            return $vcvars
        }
    }

    throw "vcvars64.bat was not found. Install Visual Studio Build Tools with the MSVC x64 toolchain."
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)]
        [string] $FilePath,

        [Parameter(Mandatory)]
        [string[]] $Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($Arguments -join ' ')"
    }
}

function Copy-VCRuntime {
    param(
        [Parameter(Mandatory)]
        [string] $Destination
    )

    $redistRoots = @(
        "C:\Program Files (x86)\Microsoft Visual Studio\18",
        "C:\Program Files\Microsoft Visual Studio\18",
        "C:\Program Files\Microsoft Visual Studio\2022",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022"
    )

    $crtDirs = @()
    foreach ($root in $redistRoots) {
        if (Test-Path -LiteralPath $root) {
            $crtDirs += Get-ChildItem -LiteralPath $root -Recurse -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match "\\VC\\Redist\\MSVC\\[^\\]+\\$Platform\\Microsoft\.VC\d+\.CRT$" }
        }
    }

    $crtDir = $crtDirs | Sort-Object FullName -Descending | Select-Object -First 1
    if ($null -eq $crtDir) {
        Write-Warning "MSVC runtime redist folder was not found. The release may require the Microsoft Visual C++ Redistributable on target machines."
        return
    }

    Copy-Item -Path (Join-Path $crtDir.FullName "*.dll") -Destination $Destination -Force
}

function New-Directory {
    param([Parameter(Mandatory)][string] $Path)

    $Path = Assert-BuildOutputPath $Path

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }

    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Assert-BuildOutputPath([string] $Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    $allowedRoots = @('out', 'releases') | ForEach-Object { [IO.Path]::GetFullPath((Join-Path $repoRoot $_)) + '\' }
    if (!(@($allowedRoots | Where-Object { $resolved.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }).Count)) {
        throw 'Build output must be inside a subdirectory of out or releases.'
    }
    for ($ancestor = $resolved; $ancestor.Length -gt $repoRoot.Length; $ancestor = [IO.Path]::GetDirectoryName($ancestor)) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Reparse point in build output: $ancestor" }
    }
    return $resolved
}

function Remove-BuildDirectory([string] $Path) {
    $checked = Assert-BuildOutputPath $Path
    if (Test-Path -LiteralPath $checked) { Remove-Item -LiteralPath $checked -Recurse -Force }
}

function Verify-PortableArchive {
    # Verify the delivered ZIP in temporary staging. Runnable development copies
    # are published separately, with build numbers, by Build Dev.ps1.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $extracted = Assert-BuildOutputPath (Join-Path $packageWorkRoot 'portable-verified')
    New-Directory $extracted
    [IO.Compression.ZipFile]::ExtractToDirectory($portableZip, $extracted)

    $expectedFiles = @(Get-ChildItem -LiteralPath $portableStage -Recurse -File)
    $actualFiles = @(Get-ChildItem -LiteralPath $extracted -Recurse -File)
    if ($expectedFiles.Count -ne $actualFiles.Count) { throw 'Portable ZIP file count does not match staging.' }
    $fileHashes = [ordered]@{}
    foreach ($file in $expectedFiles) {
        $relative = $file.FullName.Substring($portableStage.Length).TrimStart('\')
        $expandedFile = Join-Path $extracted $relative
        if (!(Test-Path -LiteralPath $expandedFile -PathType Leaf)) { throw "Portable ZIP is missing '$relative'." }
        $expectedHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        if ((Get-FileHash -LiteralPath $expandedFile -Algorithm SHA256).Hash -ne $expectedHash) {
            throw "Portable ZIP contains an inconsistent file: '$relative'."
        }
        $fileHashes[$relative.Replace('\', '/')] = $expectedHash
    }
    foreach ($required in @($exeName, 'LightHostModernScanner.exe', 'LightHostModernWorker.exe', 'LightHostModernUpdateHelper.exe',
        'LICENSE', 'THIRD-PARTY-NOTICES.txt',
        "WinUI/x64/$Configuration/LightHostModern.WinUI/LightHostModernWinUI.exe",
        "WinUI/x64/$Configuration/LightHostModern.WinUI/MainWindow.xbf",
        "WinUI/x64/$Configuration/LightHostModern.WinUI/SettingsPageView.xbf")) {
        $payloadName = ('versions/' + $portableLayout.initial.id + '/' + $required)
        if (!$fileHashes.Contains($payloadName)) { throw "Required portable component is missing: '$payloadName'." }
    }

    [ordered]@{
        status = 'passed'
        version = $appVersion
        verifiedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        archive = $portableZip
        archiveSha256 = (Get-FileHash -LiteralPath $portableZip -Algorithm SHA256).Hash
        extractedFromDeliveredArchive = $true
        verifiedFileCount = $expectedFiles.Count
        files = $fileHashes
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outRoot 'portable-verification.json') -Encoding UTF8
}

function Resolve-Wix {
    $candidates = @()
    $command = Get-Command wix.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        $candidates += $command.Source
    }

    $candidates += @(
        (Join-Path $repoRoot "tools\wix\wix.exe"),
        (Join-Path $env:USERPROFILE ".dotnet\tools\wix.exe")
    )

    foreach ($candidate in $candidates | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "WiX Toolset was not found. Install it with: dotnet tool install --tool-path tools\wix wix"
}

function ConvertTo-WixXmlText {
    param([AllowNull()][string] $Value)

    if ($null -eq $Value) {
        return ""
    }

    return [System.Security.SecurityElement]::Escape($Value)
}

function ConvertTo-RtfText {
    param([AllowNull()][string] $Value)

    if ($null -eq $Value) {
        return ""
    }

    return $Value.Replace("\", "\\").Replace("{", "\{").Replace("}", "\}").Replace("`r`n", "\par ").Replace("`n", "\par ")
}

function New-StableGuid {
    param([Parameter(Mandatory)][string] $Identity)

    $md5 = [System.Security.Cryptography.MD5]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Identity)
        $hash = $md5.ComputeHash($bytes)
        return ([Guid]::new($hash)).ToString().ToUpperInvariant()
    }
    finally {
        $md5.Dispose()
    }
}

function New-WixMsiPackage {
    param(
        [Parameter(Mandatory)][string] $SourceDir,
        [Parameter(Mandatory)][string] $WorkDir,
        [Parameter(Mandatory)][string] $TargetMsi,
        [Parameter(Mandatory)][string] $IconPath
    )

    $wix = Resolve-Wix

    New-Directory -Path $WorkDir
    if (Test-Path -LiteralPath $TargetMsi) {
        Remove-Item -LiteralPath $TargetMsi -Force
    }

    $licenseRtf = Join-Path $WorkDir "License.rtf"
    $licenseText = @"
$appName

The project's original license grant is provided in LICENSE. Bundled components have their own license terms and copyright notices.

This release uses the GPLv3 option of the original GPL-2.0-or-later grant, combined with JUCE under AGPLv3 and ASIO under GPLv3. Other components retain their own terms.

The installed application includes LICENSE, THIRD-PARTY-NOTICES.txt and the Licenses folder. Please read these files for the applicable terms. Source code and build instructions are available from the project repository.
"@
    "{\rtf1\ansi\deff0{\fonttbl{\f0 Segoe UI;}}\fs20 " + (ConvertTo-RtfText $licenseText) + "}" |
        Set-Content -LiteralPath $licenseRtf -Encoding ASCII

    $script:WixDirectoryCounter = 0
    $script:WixComponentCounter = 0
    $script:WixFileCounter = 0
    $componentRefs = New-Object System.Collections.Generic.List[string]
    $excludedMsiLanguageResourceDirectories = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    [void] $excludedMsiLanguageResourceDirectories.Add("gd-gb")
    [void] $excludedMsiLanguageResourceDirectories.Add("mi-NZ")
    [void] $excludedMsiLanguageResourceDirectories.Add("ug-CN")

    function New-WixDirectoryId {
        $script:WixDirectoryCounter++
        return "DIR_$script:WixDirectoryCounter"
    }

    function New-WixComponentId {
        $script:WixComponentCounter++
        return "CMP_$script:WixComponentCounter"
    }

    function New-WixFileId {
        $script:WixFileCounter++
        return "FIL_$script:WixFileCounter"
    }

    function Add-WixDirectoryContent {
        param(
            [Parameter(Mandatory)][string] $DirectoryPath,
            [System.Collections.Generic.List[string]] $Lines,
            [Parameter(Mandatory)][int] $IndentLevel
        )

        $indent = " " * $IndentLevel
        foreach ($file in Get-ChildItem -LiteralPath $DirectoryPath -File | Sort-Object Name) {
            $componentId = New-WixComponentId
            $fileId = if ($file.Name -eq "LightHostModernUpdateHelper.exe" -and $DirectoryPath -eq $SourceDir) { "MigrationHelperFile" } else { New-WixFileId }
            $componentRefs.Add($componentId)
            $source = ConvertTo-WixXmlText $file.FullName
            $Lines.Add("$indent<Component Id=`"$componentId`" Guid=`"*`">")
            $Lines.Add("$indent  <File Id=`"$fileId`" Source=`"$source`" KeyPath=`"yes`" />")
            $Lines.Add("$indent</Component>")
        }

        foreach ($directory in Get-ChildItem -LiteralPath $DirectoryPath -Directory | Sort-Object Name) {
            if ($excludedMsiLanguageResourceDirectories.Contains($directory.Name)) {
                continue
            }

            $directoryId = New-WixDirectoryId
            $directoryName = ConvertTo-WixXmlText $directory.Name
            $Lines.Add("$indent<Directory Id=`"$directoryId`" Name=`"$directoryName`">")
            Add-WixDirectoryContent -DirectoryPath $directory.FullName -Lines $Lines -IndentLevel ($IndentLevel + 2)
            $Lines.Add("$indent</Directory>")
        }
    }

    $installDirectoryLines = New-Object System.Collections.Generic.List[string]
    Add-WixDirectoryContent -DirectoryPath $SourceDir -Lines $installDirectoryLines -IndentLevel 10

    $featureRefs = New-Object System.Collections.Generic.List[string]
    foreach ($componentId in $componentRefs) {
        $featureRefs.Add("      <ComponentRef Id=`"$componentId`" />")
    }

    $wxsPath = Join-Path $WorkDir "LightHostModern.wxs"
    $productName = ConvertTo-WixXmlText $appName
    # Transitional MSI identity accepted by installed 1.3.x updaters.
    $msiProductName = "Light Host Modern"
    $manufacturer = "LightHostModern"
    $escapedIconPath = ConvertTo-WixXmlText $IconPath
    $installerActions = Join-Path $buildDirectory "installer-actions\$Configuration\LightHostModernInstallerActions.dll"
    if (!(Test-Path -LiteralPath $installerActions -PathType Leaf)) { throw 'Rebuild the installer actions before creating the MSI.' }
    $escapedActionsPath = ConvertTo-WixXmlText $installerActions
    $upgradeCode = "8F28E61C-DC90-4927-B7B4-3E74E4B5960B"
    $productCode = New-StableGuid "$upgradeCode|$appVersion"
    $mainExeTarget = "[APPLICATIONFOLDER]$exeName"

    $wxs = New-Object System.Collections.Generic.List[string]
    $wxs.Add("<?xml version=`"1.0`" encoding=`"UTF-8`"?>")
    $wxs.Add("<Wix xmlns=`"http://wixtoolset.org/schemas/v4/wxs`" xmlns:ui=`"http://wixtoolset.org/schemas/v4/wxs/ui`" xmlns:util=`"http://wixtoolset.org/schemas/v4/wxs/util`">")
    $wxs.Add("  <Package Name=`"$msiProductName`" Manufacturer=`"$manufacturer`" Version=`"$appVersion`" UpgradeCode=`"$upgradeCode`" ProductCode=`"$productCode`" Scope=`"perMachine`">")
    $wxs.Add("    <MajorUpgrade Schedule=`"afterInstallInitialize`" AllowSameVersionUpgrades=`"yes`" DowngradeErrorMessage=`"A newer version of $productName is already installed.`" />")
    $wxs.Add("    <MediaTemplate EmbedCab=`"yes`" />")
    $wxs.Add("    <Property Id=`"APPLICATIONFOLDER`" Secure=`"yes`" />")
    $wxs.Add("    <Binary Id=`"InstallerActions`" SourceFile=`"$escapedActionsPath`" />")
    $wxs.Add("    <CustomAction Id=`"PreserveInstallLocation`" BinaryRef=`"InstallerActions`" DllEntry=`"PreserveInstallLocation`" Execute=`"firstSequence`" Return=`"check`" />")
    $wxs.Add("    <InstallUISequence><Custom Action=`"PreserveInstallLocation`" Before=`"CostInitialize`" Condition=`"NOT Installed`" /></InstallUISequence>")
    $wxs.Add("    <CustomAction Id=`"MigrateLegacyPayload`" FileRef=`"MigrationHelperFile`" ExeCommand=`"--migrate-legacy-install`" Execute=`"commit`" Impersonate=`"yes`" Return=`"ignore`" />")
    $wxs.Add("    <InstallExecuteSequence><Custom Action=`"PreserveInstallLocation`" Before=`"CostInitialize`" Condition=`"NOT Installed`" /><Custom Action=`"MigrateLegacyPayload`" After=`"InstallFiles`" Condition=`"NOT Installed`" /></InstallExecuteSequence>")
    $wxs.Add("    <Icon Id=`"AppIcon.ico`" SourceFile=`"$escapedIconPath`" />")
    $wxs.Add("    <Property Id=`"ARPPRODUCTICON`" Value=`"AppIcon.ico`" />")
    $wxs.Add("    <Property Id=`"ApplicationFolderName`" Value=`"$productName`" />")
    $wxs.Add("    <Property Id=`"WIXUI_INSTALLDIR`" Value=`"APPLICATIONFOLDER`" />")
    $wxs.Add("    <SetProperty Id=`"ARPINSTALLLOCATION`" Value=`"[APPLICATIONFOLDER]`" After=`"CostFinalize`" Sequence=`"both`" />")
    $wxs.Add("    <WixVariable Id=`"WixUILicenseRtf`" Value=`"$licenseRtf`" />")
    $wxs.Add("    <ui:WixUI Id=`"WixUI_InstallDir`" />")
    $wxs.Add("    <StandardDirectory Id=`"ProgramFiles64Folder`">")
    $wxs.Add("      <Directory Id=`"APPLICATIONFOLDER`" Name=`"$productName`">")
    foreach ($line in $installDirectoryLines) {
        $wxs.Add($line)
    }
    $wxs.Add("      </Directory>")
    $wxs.Add("    </StandardDirectory>")
    $wxs.Add("    <StandardDirectory Id=`"ProgramMenuFolder`">")
    $wxs.Add("      <Directory Id=`"ApplicationProgramsFolder`" Name=`"$productName`">")
    $wxs.Add("        <Component Id=`"StartMenuShortcutComponent`" Guid=`"*`">")
    $wxs.Add("          <Shortcut Id=`"StartMenuShortcut`" Name=`"$productName`" Description=`"$productName`" Target=`"$mainExeTarget`" WorkingDirectory=`"APPLICATIONFOLDER`" Icon=`"AppIcon.ico`" />")
    $wxs.Add("          <RemoveFolder Id=`"RemoveApplicationProgramsFolder`" On=`"uninstall`" />")
    $wxs.Add("          <RegistryValue Root=`"HKCU`" Key=`"Software\LightHostModern`" Name=`"StartMenuShortcut`" Type=`"integer`" Value=`"1`" KeyPath=`"yes`" />")
    $wxs.Add("        </Component>")
    $wxs.Add("      </Directory>")
    $wxs.Add("    </StandardDirectory>")
    $wxs.Add("    <StandardDirectory Id=`"DesktopFolder`">")
    $wxs.Add("      <Component Id=`"DesktopShortcutComponent`" Guid=`"*`">")
    $wxs.Add("        <Shortcut Id=`"DesktopShortcut`" Name=`"$productName`" Description=`"$productName`" Target=`"$mainExeTarget`" WorkingDirectory=`"APPLICATIONFOLDER`" Icon=`"AppIcon.ico`" />")
    $wxs.Add("        <RegistryValue Root=`"HKCU`" Key=`"Software\LightHostModern`" Name=`"DesktopShortcut`" Type=`"integer`" Value=`"1`" KeyPath=`"yes`" />")
    $wxs.Add("      </Component>")
    $wxs.Add("    </StandardDirectory>")
    $wxs.Add("    <Feature Id=`"ApplicationFeature`" Title=`"$productName`" Level=`"1`">")
    foreach ($line in $featureRefs) {
        $wxs.Add($line)
    }
    $wxs.Add("    </Feature>")
    $wxs.Add("    <Feature Id=`"StartMenuShortcutFeature`" Title=`"Start menu shortcut`" Level=`"1`">")
    $wxs.Add("      <ComponentRef Id=`"StartMenuShortcutComponent`" />")
    $wxs.Add("    </Feature>")
    $wxs.Add("    <Feature Id=`"DesktopShortcutFeature`" Title=`"Desktop shortcut`" Level=`"1`">")
    $wxs.Add("      <ComponentRef Id=`"DesktopShortcutComponent`" />")
    $wxs.Add("    </Feature>")
    $wxs.Add("  </Package>")
    $wxs.Add("</Wix>")
    $wxs | Set-Content -LiteralPath $wxsPath -Encoding UTF8

    $wixVersionOutput = & $wix --version
    $wixVersion = ($wixVersionOutput | Select-Object -First 1).Trim()
    $wixExtensionPackage = "WixToolset.UI.wixext/$wixVersion"
    $wixUtilExtensionPackage = "WixToolset.Util.wixext/$wixVersion"

    & $wix extension add $wixExtensionPackage | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to install or enable the WiX UI extension."
    }

    & $wix extension add $wixUtilExtensionPackage | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to install or enable the WiX Util extension."
    }

    & $wix build $wxsPath -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext -arch x64 -o $TargetMsi | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "WiX failed to build the MSI installer."
    }

    $wixPdb = [System.IO.Path]::ChangeExtension($TargetMsi, ".wixpdb")
    if (Test-Path -LiteralPath $wixPdb) {
        Remove-Item -LiteralPath $wixPdb -Force
    }
}

$outRoot = Assert-BuildOutputPath $outRoot
if (!$SkipBuild) {
    $msbuild = Resolve-MSBuild
    Restore-ProjectNuGet -MSBuildPath $msbuild
    $winUIProject = Join-Path $repoRoot "WinUI\LightHostModern.WinUI.sln"

    Invoke-Checked -FilePath $msbuild -Arguments (@(
        $winUIProject,
        "/m",
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform"
    ) + @(Get-LocalNuGetArguments))

    $cmakePath = Resolve-CMake
    $configureArgs = @(
        "--preset", $Preset,
        "-DLIGHTHOST_ENABLE_VST2=$EnableVst2",
        "-DLIGHTHOST_VST2_PROVIDER=$Vst2Provider",
        "-DLIGHTHOST_REALTIME_AUDIT=OFF"
    )
    $configureArgs += @(Get-LocalDependencyArguments)

    if (![string]::IsNullOrWhiteSpace($Vst2SdkDir)) {
        $configureArgs += "-DLIGHTHOST_VST2_SDK_DIR=$Vst2SdkDir"
    } elseif (![string]::IsNullOrWhiteSpace($env:LIGHTHOST_VST2_SDK_DIR)) {
        $configureArgs += "-DLIGHTHOST_VST2_SDK_DIR=$env:LIGHTHOST_VST2_SDK_DIR"
    }

    Invoke-Checked -FilePath $cmakePath -Arguments $configureArgs
    Invoke-Checked -FilePath $cmakePath -Arguments @("--build", "--preset", "$Preset-$($Configuration.ToLowerInvariant())", "--config", $Configuration)
}

$buildDirectory = Join-Path $repoRoot "out\build\$Preset"
if (!$SkipTests) {
    $ctestPath = Join-Path (Split-Path (Resolve-CMake) -Parent) 'ctest.exe'
    Invoke-Checked -FilePath $ctestPath -Arguments @('--test-dir', $buildDirectory, '-C', $Configuration, '--output-on-failure')
}
$hostOutput = Join-Path $buildDirectory "LightHostModern_artefacts\$Configuration"
$hostExe = Join-Path $hostOutput $exeName
$winUIStageScript = Join-Path $PSScriptRoot 'WinUI Output.ps1'
$builtWinUIOutput = & $winUIStageScript -Mode Resolve -Platform $Platform -Configuration $Configuration
$hostWinUIOutput = Join-Path $hostOutput "WinUI\$Platform\$Configuration\LightHostModern.WinUI"
$winUIOutput = Join-Path $hostWinUIOutput "LightHostModernWinUI.exe"

if (!(Test-Path -LiteralPath (Join-Path $builtWinUIOutput "LightHostModernWinUI.exe"))) {
    throw "Built WinUI output was not found: $builtWinUIOutput"
}

& $winUIStageScript -Mode Stage -Platform $Platform -Configuration $Configuration -Destination $hostWinUIOutput

if (!(Test-Path -LiteralPath $hostExe)) {
    throw "Host executable was not found: $hostExe"
}

if (!(Test-Path -LiteralPath $winUIOutput)) {
    throw "WinUI executable was not found inside the host output. Build the WinUI project before building the host: $winUIOutput"
}

New-Directory -Path $stageRoot
if (!(Test-Path -LiteralPath (Join-Path $hostOutput 'LightHostModernScanner.exe'))) {
    throw 'LightHostModernScanner.exe is missing from the host build output.'
}
if (!(Test-Path -LiteralPath (Join-Path $hostOutput 'LightHostModernUpdateHelper.exe'))) { throw 'LightHostModernUpdateHelper.exe is missing from the host build output.' }
foreach ($releaseExecutable in @($hostExe, $winUIOutput, (Join-Path $hostOutput 'LightHostModernScanner.exe'), (Join-Path $hostOutput 'LightHostModernWorker.exe'), (Join-Path $hostOutput 'LightHostModernUpdateHelper.exe'))) {
    $versionInfo = (Get-Item -LiteralPath $releaseExecutable).VersionInfo
    if ($versionInfo.ProductVersion -ne $appVersion -or $versionInfo.FileVersion -ne $appVersion) {
        throw "Executable version does not match release ${appVersion}: $releaseExecutable (product: $($versionInfo.ProductVersion), file: $($versionInfo.FileVersion)). Rebuild before packaging."
    }
}
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
foreach ($name in $exeName, 'LightHostModernScanner.exe', 'LightHostModernWorker.exe', 'LightHostModernUpdateHelper.exe', 'Light Host Modern.exe', 'LightHostScanner.exe', 'LightHostUpdateHelper.exe', 'LightHostWinUI.exe', 'WinUI') {
    Copy-Item -LiteralPath (Join-Path $hostOutput $name) -Destination $stageRoot -Recurse -Force
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'license') -Destination (Join-Path $stageRoot 'LICENSE')
foreach ($name in 'README.md', 'THIRD-PARTY-NOTICES.txt') {
    Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination $stageRoot
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'ThirdParty\Licenses') -Destination (Join-Path $stageRoot 'Licenses') -Recurse

Get-ChildItem -LiteralPath $stageRoot -Recurse -File |
    Where-Object { $_.Extension -in @(".pdb", ".ilk", ".exp", ".lib", ".appxsym") } |
    Remove-Item -Force

Copy-VCRuntime -Destination $stageRoot

if (![string]::IsNullOrWhiteSpace($SigningThumbprint)) {
    $signTargets = @(
        (Join-Path $stageRoot $exeName),
        (Join-Path $stageRoot "LightHostModernScanner.exe"),
        (Join-Path $stageRoot "LightHostModernWorker.exe"),
        (Join-Path $stageRoot "LightHostModernUpdateHelper.exe"),
        (Join-Path $stageRoot "WinUI\x64\$Configuration\LightHostModern.WinUI\LightHostModernWinUI.exe"),
        (Join-Path $stageRoot "WinUI\x64\$Configuration\LightHostModern.WinUI\RestartAgent.exe")
    )
    foreach ($signTarget in $signTargets) {
        if (Test-Path -LiteralPath $signTarget) {
            Sign-ReleaseFile -Path $signTarget
        }
    }
} else {
    Write-Warning "Release signing is not configured. Public releases should set LIGHTHOST_SIGNING_THUMBPRINT to a trusted code-signing certificate."
}

$releaseInfo = [ordered]@{
    name = $appName
    version = $appVersion
    configuration = $Configuration
    platform = $Platform
    builtAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    entryPoint = $exeName
    uiSha256 = (Get-FileHash -LiteralPath (Join-Path $stageRoot "WinUI\x64\$Configuration\LightHostModern.WinUI\LightHostModernWinUI.exe") -Algorithm SHA256).Hash
}

$releaseInfo | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stageRoot "release-info.json") -Encoding UTF8

$forbidden = @(Get-ChildItem -LiteralPath $stageRoot -Recurse -File | Where-Object {
    $_.Extension -in '.vst3', '.vst', '.clap', '.pfx', '.p12', '.key', '.pem', '.pvk', '.snk' -or $_.Name -match 'Dragonfly|LightHostModern.*Tests|Fixture' -or $_.FullName -match '[\\/](fixtures|test-profiles|Tests)[\\/]'
})
if ($forbidden.Count) { throw "Test files or third-party plugin fixtures found in payload: $($forbidden.FullName -join ', ')" }

if (Test-Path -LiteralPath $portableZip) {
    Remove-Item -LiteralPath $portableZip -Force
}

# Explicit legacy payload allowlist. No wildcard or recursive removal at install time.
$legacyFiles = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse | ForEach-Object {
    $relative = $_.FullName.Substring($stageRoot.Length + 1).Replace('\', '/')
    $relative.Replace('LightHostModern.WinUI', 'LightHost.WinUI').Replace('LightHostModernWinUI', 'LightHostWinUI').Replace('LightHostModernScanner', 'LightHostScanner').Replace('LightHostModernUpdateHelper', 'LightHostUpdateHelper').Replace('LightHostModern.exe', 'Light Host Modern.exe')
} | Sort-Object -Unique)
# This installer-owned file was created after extracting the 1.2.x ZIP, so it
# is absent from the application payload inventory. Back it up; never run it.
$legacyFiles = @($legacyFiles + 'Uninstall-LightHostModern.ps1' | Sort-Object -Unique)
ConvertTo-Json -InputObject $legacyFiles | Set-Content -LiteralPath (Join-Path $stageRoot 'legacy-payload-files.json') -Encoding UTF8

$portableStage = Join-Path $packageWorkRoot 'portable'
$launcher = Join-Path $hostOutput 'LightHostModernLauncher.exe'
Sign-ReleaseFile -Path $launcher
$portableLayout = New-VersionedPortable $stageRoot $portableStage $launcher
Compress-Archive -Path (Join-Path $portableStage "*") -DestinationPath $portableZip -Force

$installerWork = Join-Path $packageWorkRoot "installer"

$legacyInstallerExe = Join-Path $outRoot "LightHostModern-Setup.exe"
if (Test-Path -LiteralPath $legacyInstallerExe) {
    try {
        Remove-Item -LiteralPath $legacyInstallerExe -Force
    } catch {
        Write-Warning "Could not remove legacy setup executable '$legacyInstallerExe': $($_.Exception.Message)"
    }
}

New-WixMsiPackage -SourceDir $stageRoot -WorkDir $installerWork -TargetMsi $installerMsi -IconPath $releaseIcon
Sign-ReleaseFile -Path $installerMsi

$artifactMetadata = foreach ($pair in @(@($installerMsi, 'installed'), @($portableZip, 'portable'))) {
    $file = Get-Item -LiteralPath $pair[0]
    $digest = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    & (Join-Path $hostOutput 'LightHostModernUpdateHelper.exe') --mode validate --operation $outRoot --package $file.FullName --version $appVersion --size $file.Length --sha256 $digest --distribution $pair[1]
    if ($LASTEXITCODE -ne 0) { throw "Package inspection failed: $(Get-Content -LiteralPath (Join-Path $outRoot 'update-result.json') -Raw)" }
    [ordered]@{ name = $file.Name; version = $appVersion; architecture = $Platform; distribution = $pair[1]; size = $file.Length; digest = "sha256:$digest" }
}
[ordered]@{ formatVersion = 1; artifacts = @($artifactMetadata) } | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $outRoot 'release-artifacts.json') -Encoding UTF8
if ($ManifestSigningThumbprint) {
    Write-SignedUpdateManifest $outRoot $appVersion $artifactMetadata $portableLayout.initial.inventoryHash $ManifestSigningThumbprint
    foreach ($artifact in $artifactMetadata) {
        & (Join-Path $hostOutput 'LightHostModernUpdateHelper.exe') --mode validate-signed --operation $outRoot --package (Join-Path $outRoot $artifact.name) --version $appVersion --size $artifact.size --sha256 $artifact.digest --distribution $artifact.distribution
        if ($LASTEXITCODE -ne 0) { throw 'Signed release was rejected by the embedded trust roots. Configure and rebuild UpdateTrustKeys.h before publishing.' }
    }
} else {
    Write-Warning 'Update-manifest signing is not configured. This package supports manual installation only.'
}

$legacyPortableExe = Join-Path $outRoot "LightHostModern-Portable.exe"
if (Test-Path -LiteralPath $legacyPortableExe) {
    Remove-Item -LiteralPath $legacyPortableExe -Force
}

Verify-PortableArchive

# Release packages always carry their version. Remove only the known obsolete
# aliases from this validated output directory, so a later upload cannot pick
# them up accidentally.
foreach ($obsoleteName in 'LightHostModern-Setup.msi', 'LightHostModern-Portable.zip') {
    $obsoletePackage = Join-Path $outRoot $obsoleteName
    if (Test-Path -LiteralPath $obsoletePackage -PathType Leaf) {
        Remove-Item -LiteralPath $obsoletePackage -Force
    }
}

$checksumNames = @([IO.Path]::GetFileName($installerMsi), [IO.Path]::GetFileName($portableZip), 'release-artifacts.json')
if ($ManifestSigningThumbprint) {
    $checksumNames += 'update-manifest.json', 'update-manifest.sig'
}
$checksumLines = foreach ($name in $checksumNames | Sort-Object) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $outRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $name"
}
[IO.File]::WriteAllText((Join-Path $outRoot 'SHA256SUMS.txt'), (($checksumLines -join "`n") + "`n"), (New-Object Text.UTF8Encoding($false)))

if (!$KeepStage) {
    Remove-BuildDirectory $stageRoot
    Remove-BuildDirectory $packageWorkRoot
}

Write-Host ""
Write-Host "Release artifacts created:"
Write-Host "  Installer: $installerMsi"
Write-Host "  Portable:  $portableZip"
