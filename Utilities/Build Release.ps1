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

    [switch] $KeepStage
)

$ErrorActionPreference = "Stop"

$appName = "Light Host Modern"
$appVersion = "1.1.1"
$exeName = "Light Host Modern.exe"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$outRoot = Join-Path $repoRoot "out\release"
$stageRoot = Join-Path $outRoot "payload"
$packageWorkRoot = Join-Path $outRoot "package-work"
$payloadZip = Join-Path $outRoot "LightHostModern-payload.zip"
$installerMsi = Join-Path $outRoot "LightHostModern-Setup.msi"
$portableExe = Join-Path $outRoot "LightHostModern-Portable.exe"
$releaseIcon = Join-Path $repoRoot "Icon\logo.ico"

function Resolve-CMake {
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -ne $cmake) {
        return $cmake.Source
    }

    $defaultCMake = Join-Path $env:ProgramFiles "CMake\bin\cmake.exe"
    if (Test-Path -LiteralPath $defaultCMake) {
        return $defaultCMake
    }

    throw "CMake 3.22+ was not found. Install current CMake and Visual Studio Build Tools 2022."
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

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }

    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function New-IExpressPackage {
    param(
        [Parameter(Mandatory)]
        [string] $Name,

        [Parameter(Mandatory)]
        [string] $SourceDir,

        [Parameter(Mandatory)]
        [string] $Launcher,

        [Parameter(Mandatory)]
        [string] $TargetExe
    )

    $iexpress = Join-Path $env:windir "System32\iexpress.exe"
    if (!(Test-Path -LiteralPath $iexpress)) {
        throw "iexpress.exe was not found. It is required to generate the single-file release executables."
    }

    $files = Get-ChildItem -LiteralPath $SourceDir -File | Sort-Object Name
    if ($files.Count -eq 0) {
        throw "No files found for IExpress package: $SourceDir"
    }

    $sedPath = Join-Path $SourceDir "$Name.sed"
    $fileStrings = New-Object System.Collections.Generic.List[string]
    $sourceFileEntries = New-Object System.Collections.Generic.List[string]

    for ($i = 0; $i -lt $files.Count; $i++) {
        $fileStrings.Add("FILE$i=$($files[$i].Name)")
        $sourceFileEntries.Add("%FILE$i%=")
    }

    $sed = @"
[Version]
Class=IEXPRESS
SEDVersion=3

[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=1
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=<None>
AdminQuietInstCmd=%AppLaunched%
UserQuietInstCmd=%AppLaunched%
SourceFiles=SourceFiles

[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=
TargetName=$TargetExe
FriendlyName=$Name
AppLaunched=$Launcher
$($fileStrings -join "`r`n")

[SourceFiles]
SourceFiles0=$SourceDir

[SourceFiles0]
$($sourceFileEntries -join "`r`n")
"@

    Set-Content -LiteralPath $sedPath -Value $sed -Encoding ASCII
    Invoke-Checked -FilePath $iexpress -Arguments @("/N", "/Q", $sedPath)

    if (!(Test-Path -LiteralPath $TargetExe)) {
        throw "IExpress did not create the expected package: $TargetExe"
    }
}

function New-NativeSelfExtractPackage {
    param(
        [Parameter(Mandatory)]
        [string] $Name,

        [Parameter(Mandatory)]
        [string] $WorkDir,

        [Parameter(Mandatory)]
        [string] $PayloadZipPath,

        [Parameter(Mandatory)]
        [string] $EntryScriptPath,

        [Parameter(Mandatory)]
        [string] $TargetExe,

        [string] $IconPath = "",

        [switch] $InstallerUi
    )

    $vcvars = Resolve-VCVars64
    $safeName = ($Name -replace "[^A-Za-z0-9_]", "_")
    $cppPath = Join-Path $WorkDir "$safeName.cpp"
    $rcPath = Join-Path $WorkDir "$safeName.rc"
    $resPath = Join-Path $WorkDir "$safeName.res"
    $objPath = Join-Path $WorkDir "$safeName.obj"

    $cpp = @'
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <commctrl.h>
#include <string>

static bool writeResource(int id, const std::wstring& path)
{
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (resource == nullptr) return false;

    HGLOBAL loaded = LoadResource(nullptr, resource);
    if (loaded == nullptr) return false;

    DWORD size = SizeofResource(nullptr, resource);
    const void* data = LockResource(loaded);
    if (data == nullptr || size == 0) return false;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(file, data, size, &written, nullptr);
    CloseHandle(file);
    return ok && written == size;
}

static std::wstring quote(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

static bool showInstallPrompt()
{
    HMODULE comctl = LoadLibraryW(L"comctl32.dll");
    if (comctl != nullptr) {
        using TaskDialogIndirectFn = HRESULT (WINAPI *)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
        auto taskDialogIndirect = reinterpret_cast<TaskDialogIndirectFn>(GetProcAddress(comctl, "TaskDialogIndirect"));

        if (taskDialogIndirect != nullptr) {
            const TASKDIALOG_BUTTON buttons[] = {
                { 100, L"Install Light Host Modern\nInstall for the current Windows user." },
                { IDCANCEL, L"Cancel" }
            };

            TASKDIALOGCONFIG config = {};
            config.cbSize = sizeof(config);
            config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
            config.pszWindowTitle = L"Light Host Modern Setup";
            config.pszMainInstruction = L"Install Light Host Modern";
            config.pszContent = L"Destination: %LOCALAPPDATA%\\Programs\\Light Host Modern\n\nThe installer will add Start Menu and Desktop shortcuts.";
            config.cButtons = ARRAYSIZE(buttons);
            config.pButtons = buttons;
            config.nDefaultButton = 100;
            config.pszMainIcon = TD_INFORMATION_ICON;

            int pressedButton = IDCANCEL;
            if (SUCCEEDED(taskDialogIndirect(&config, &pressedButton, nullptr, nullptr))) {
                FreeLibrary(comctl);
                return pressedButton == 100;
            }
        }

        FreeLibrary(comctl);
    }

    int result = MessageBoxW(
        nullptr,
        L"Install Light Host Modern for the current Windows user?\n\nDestination: %LOCALAPPDATA%\\Programs\\Light Host Modern",
        L"Light Host Modern Setup",
        MB_OKCANCEL | MB_ICONINFORMATION);

    return result == IDOK;
}

static void showInstallComplete()
{
    HMODULE comctl = LoadLibraryW(L"comctl32.dll");
    if (comctl != nullptr) {
        using TaskDialogFn = HRESULT (WINAPI *)(HWND, HINSTANCE, PCWSTR, PCWSTR, PCWSTR, TASKDIALOG_COMMON_BUTTON_FLAGS, PCWSTR, int*);
        auto taskDialog = reinterpret_cast<TaskDialogFn>(GetProcAddress(comctl, "TaskDialog"));

        if (taskDialog != nullptr) {
            int pressedButton = IDOK;
            taskDialog(
                nullptr,
                nullptr,
                L"Light Host Modern Setup",
                L"Installation completed",
                L"Light Host Modern was installed successfully.",
                TDCBF_OK_BUTTON,
                TD_INFORMATION_ICON,
                &pressedButton);
            FreeLibrary(comctl);
            return;
        }

        FreeLibrary(comctl);
    }

    MessageBoxW(nullptr, L"Light Host Modern was installed successfully.", L"Light Host Modern Setup", MB_OK | MB_ICONINFORMATION);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    wchar_t tempPath[MAX_PATH] = {};
    DWORD tempLength = GetTempPathW(MAX_PATH, tempPath);
    if (tempLength == 0 || tempLength >= MAX_PATH) {
        MessageBoxW(nullptr, L"Unable to resolve the temporary folder.", L"Light Host Modern", MB_ICONERROR);
        return 1;
    }

    std::wstring workDir = std::wstring(tempPath) + L"LightHostModernRelease-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(workDir.c_str(), nullptr);

    std::wstring payloadPath = workDir + L"\\payload.zip";
    std::wstring scriptPath = workDir + L"\\entry.ps1";

    if (!writeResource(101, payloadPath) || !writeResource(102, scriptPath)) {
        MessageBoxW(nullptr, L"Unable to extract release payload.", L"Light Host Modern", MB_ICONERROR);
        return 1;
    }

    std::wstring command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Sta -File " + quote(scriptPath);
    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION processInfo = {};
    std::wstring mutableCommand = command;
    if (!CreateProcessW(nullptr, &mutableCommand[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, workDir.c_str(), &startupInfo, &processInfo)) {
        MessageBoxW(nullptr, L"Unable to run the embedded release script.", L"Light Host Modern", MB_ICONERROR);
        return 1;
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    if (exitCode != 0) {
        MessageBoxW(nullptr, L"The release script failed. Run the app with --debug or rebuild the release package for diagnostics.", L"Light Host Modern", MB_ICONERROR);
        return static_cast<int>(exitCode);
    }

    return 0;
}
'@

    Set-Content -LiteralPath $cppPath -Value $cpp -Encoding ASCII

    $payloadResourcePath = $PayloadZipPath.Replace("\", "\\")
    $scriptResourcePath = $EntryScriptPath.Replace("\", "\\")
    $iconResourceLine = ""
    if (![string]::IsNullOrWhiteSpace($IconPath) -and (Test-Path -LiteralPath $IconPath)) {
        $iconResourcePath = $IconPath.Replace("\", "\\")
        $iconResourceLine = "1 ICON `"$iconResourcePath`"`r`n"
    }

    $rc = @"
$iconResourceLine
101 RCDATA "$payloadResourcePath"
102 RCDATA "$scriptResourcePath"
"@
    Set-Content -LiteralPath $rcPath -Value $rc -Encoding ASCII

    if (Test-Path -LiteralPath $TargetExe) {
        Remove-Item -LiteralPath $TargetExe -Force
    }

    $installerDefine = ""
    if ($InstallerUi) {
        $installerDefine = "/DSHOW_INSTALLER_UI "
    }

    $compileCommand = "`"$vcvars`" >nul && rc.exe /nologo /fo `"$resPath`" `"$rcPath`" && cl.exe /nologo /O2 /MT /EHsc /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0600 $installerDefine/Fo`"$objPath`" `"$cppPath`" `"$resPath`" user32.lib /link /SUBSYSTEM:WINDOWS /OUT:`"$TargetExe`""
    & cmd.exe /d /s /c $compileCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Native self-extracting package build failed with exit code $LASTEXITCODE`: $TargetExe"
    }

    if (!(Test-Path -LiteralPath $TargetExe)) {
        throw "Native self-extracting package was not created: $TargetExe"
    }
}

function Write-InstallerPayload {
    param([Parameter(Mandatory)][string] $TargetDir)

    New-Directory -Path $TargetDir
    Copy-Item -LiteralPath $payloadZip -Destination (Join-Path $TargetDir "payload.zip") -Force

    @'
@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -Sta -File "%~dp0install.ps1"
exit /b %ERRORLEVEL%
'@ | Set-Content -LiteralPath (Join-Path $TargetDir "install.cmd") -Encoding ASCII

    @'
$ErrorActionPreference = "Stop"

$appName = "Light Host Modern"
$exeName = "Light Host Modern.exe"
$payloadZip = Join-Path $PSScriptRoot "payload.zip"

function Get-DefaultInstallRoot {
    Join-Path $env:LOCALAPPDATA "Programs\Light Host Modern"
}

function ConvertTo-PowerShellLiteral {
    param([AllowNull()][string] $Value)
    if ($null -eq $Value) { return "''" }
    return "'" + ($Value -replace "'", "''") + "'"
}

function New-Shortcut {
    param(
        [Parameter(Mandatory)][string] $ShortcutPath,
        [Parameter(Mandatory)][string] $TargetPath,
        [Parameter(Mandatory)][string] $WorkingDirectory
    )

    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($ShortcutPath)
    $shortcut.TargetPath = $TargetPath
    $shortcut.WorkingDirectory = $WorkingDirectory
    $shortcut.IconLocation = "$TargetPath,0"
    $shortcut.Save()
}

function Install-LightHostModern {
    param(
        [Parameter(Mandatory)][string] $InstallRoot,
        [bool] $CreateStartMenu,
        [bool] $CreateDesktopShortcut,
        [bool] $LaunchAfterInstall
    )

    if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
        throw "Install location cannot be empty."
    }

    $installRoot = [System.IO.Path]::GetFullPath($InstallRoot)
    $startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Light Host Modern"
    $desktopShortcut = Join-Path ([Environment]::GetFolderPath("DesktopDirectory")) "Light Host Modern.lnk"
    $uninstallScript = Join-Path $installRoot "Uninstall-LightHostModern.ps1"

    Get-Process "Light Host Modern" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

    if (Test-Path -LiteralPath $installRoot) {
        Remove-Item -LiteralPath $installRoot -Recurse -Force
    }

    New-Item -ItemType Directory -Force -Path $installRoot | Out-Null
    Expand-Archive -LiteralPath $payloadZip -DestinationPath $installRoot -Force

    $exePath = Join-Path $installRoot $exeName
    if (!(Test-Path -LiteralPath $exePath)) {
        throw "Installed executable was not found: $exePath"
    }

    if ($CreateStartMenu) {
        New-Item -ItemType Directory -Force -Path $startMenuDir | Out-Null
        New-Shortcut -ShortcutPath (Join-Path $startMenuDir "Light Host Modern.lnk") -TargetPath $exePath -WorkingDirectory $installRoot
    }

    if ($CreateDesktopShortcut) {
        New-Shortcut -ShortcutPath $desktopShortcut -TargetPath $exePath -WorkingDirectory $installRoot
    }

    $installLiteral = ConvertTo-PowerShellLiteral $installRoot
    $startMenuLiteral = ConvertTo-PowerShellLiteral $(if ($CreateStartMenu) { $startMenuDir } else { "" })
    $desktopLiteral = ConvertTo-PowerShellLiteral $(if ($CreateDesktopShortcut) { $desktopShortcut } else { "" })

    $uninstallBody = @"
`$ErrorActionPreference = "Stop"
`$installRoot = $installLiteral
`$startMenuDir = $startMenuLiteral
`$desktopShortcut = $desktopLiteral
`$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\LightHostModern"
Get-Process "Light Host Modern" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
if (`$desktopShortcut -and (Test-Path -LiteralPath `$desktopShortcut)) { Remove-Item -LiteralPath `$desktopShortcut -Force }
if (`$startMenuDir -and (Test-Path -LiteralPath `$startMenuDir)) { Remove-Item -LiteralPath `$startMenuDir -Recurse -Force }
if (Test-Path -LiteralPath `$uninstallKey) { Remove-Item -LiteralPath `$uninstallKey -Recurse -Force }
if (Test-Path -LiteralPath `$installRoot) { Remove-Item -LiteralPath `$installRoot -Recurse -Force }
"@

    Set-Content -LiteralPath $uninstallScript -Value $uninstallBody -Encoding UTF8

    $uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\LightHostModern"
    New-Item -Path $uninstallKey -Force | Out-Null
    Set-ItemProperty -Path $uninstallKey -Name DisplayName -Value $appName
    Set-ItemProperty -Path $uninstallKey -Name DisplayVersion -Value $appVersion
    Set-ItemProperty -Path $uninstallKey -Name Publisher -Value "Light Host Modern"
    Set-ItemProperty -Path $uninstallKey -Name InstallLocation -Value $installRoot
    Set-ItemProperty -Path $uninstallKey -Name DisplayIcon -Value $exePath
    Set-ItemProperty -Path $uninstallKey -Name UninstallString -Value "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$uninstallScript`""
    Set-ItemProperty -Path $uninstallKey -Name NoModify -Value 1 -Type DWord
    Set-ItemProperty -Path $uninstallKey -Name NoRepair -Value 1 -Type DWord

    if ($LaunchAfterInstall) {
        Start-Process -FilePath $exePath -WorkingDirectory $installRoot
    }

    return $installRoot
}

function Show-InstallerWizard {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing

    [System.Windows.Forms.Application]::EnableVisualStyles()

    $state = [ordered]@{
        Page = 0
        InstallRoot = Get-DefaultInstallRoot
        CreateStartMenu = $true
        CreateDesktopShortcut = $true
        LaunchAfterInstall = $false
        InstalledPath = ""
    }

    $form = New-Object System.Windows.Forms.Form
    $form.Text = "Light Host Modern Setup"
    $form.StartPosition = "CenterScreen"
    $form.FormBorderStyle = "FixedDialog"
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false
    $form.ClientSize = New-Object System.Drawing.Size(680, 460)
    $form.Font = New-Object System.Drawing.Font("Segoe UI", 9)

    $title = New-Object System.Windows.Forms.Label
    $title.AutoSize = $false
    $title.Location = New-Object System.Drawing.Point(24, 18)
    $title.Size = New-Object System.Drawing.Size(620, 40)
    $title.Font = New-Object System.Drawing.Font("Segoe UI", 16, [System.Drawing.FontStyle]::Bold)
    $form.Controls.Add($title)

    $panel = New-Object System.Windows.Forms.Panel
    $panel.Location = New-Object System.Drawing.Point(24, 72)
    $panel.Size = New-Object System.Drawing.Size(632, 300)
    $form.Controls.Add($panel)

    $backButton = New-Object System.Windows.Forms.Button
    $backButton.Text = "Back"
    $backButton.Location = New-Object System.Drawing.Point(332, 404)
    $backButton.Size = New-Object System.Drawing.Size(96, 32)
    $form.Controls.Add($backButton)

    $nextButton = New-Object System.Windows.Forms.Button
    $nextButton.Text = "Next"
    $nextButton.Location = New-Object System.Drawing.Point(440, 404)
    $nextButton.Size = New-Object System.Drawing.Size(96, 32)
    $form.Controls.Add($nextButton)

    $cancelButton = New-Object System.Windows.Forms.Button
    $cancelButton.Text = "Cancel"
    $cancelButton.Location = New-Object System.Drawing.Point(548, 404)
    $cancelButton.Size = New-Object System.Drawing.Size(96, 32)
    $form.Controls.Add($cancelButton)

    $script:pathBox = $null
    $script:startMenuCheck = $null
    $script:desktopCheck = $null
    $script:launchCheck = $null
    $script:statusLabel = $null

    function Add-BodyLabel {
        param([string] $Text, [int] $Y, [int] $Height = 28)
        $label = New-Object System.Windows.Forms.Label
        $label.AutoSize = $false
        $label.Location = New-Object System.Drawing.Point(0, $Y)
        $label.Size = New-Object System.Drawing.Size(620, $Height)
        $label.Text = $Text
        [void] $panel.Controls.Add($label)
        return $label
    }

    function Render-Page {
        $panel.Controls.Clear()
        $backButton.Enabled = $state.Page -gt 0 -and $state.Page -lt 4
        $cancelButton.Visible = $state.Page -lt 4
        $nextButton.Enabled = $true
        $nextButton.Visible = $true

        switch ($state.Page) {
            0 {
                $title.Text = "Welcome to Light Host Modern Setup"
                Add-BodyLabel "This wizard will install Light Host Modern on your computer." 10 32 | Out-Null
                Add-BodyLabel "Click Next to continue." 58 28 | Out-Null
                $nextButton.Text = "Next"
            }
            1 {
                $title.Text = "Choose install location"
                Add-BodyLabel "Select the folder where Light Host Modern will be installed." 8 28 | Out-Null

                $script:pathBox = New-Object System.Windows.Forms.TextBox
                $script:pathBox.Location = New-Object System.Drawing.Point(0, 58)
                $script:pathBox.Size = New-Object System.Drawing.Size(512, 28)
                $script:pathBox.Text = $state.InstallRoot
                $panel.Controls.Add($script:pathBox)

                $browseButton = New-Object System.Windows.Forms.Button
                $browseButton.Text = "Browse..."
                $browseButton.Location = New-Object System.Drawing.Point(526, 56)
                $browseButton.Size = New-Object System.Drawing.Size(96, 30)
                $browseButton.Add_Click({
                    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
                    $dialog.Description = "Choose the install folder"
                    $dialog.SelectedPath = $script:pathBox.Text
                    if ($dialog.ShowDialog($form) -eq [System.Windows.Forms.DialogResult]::OK) {
                        $script:pathBox.Text = $dialog.SelectedPath
                    }
                })
                $panel.Controls.Add($browseButton)

                $nextButton.Text = "Next"
            }
            2 {
                $title.Text = "Choose setup options"
                Add-BodyLabel "Select the shortcuts and post-install actions to create." 8 28 | Out-Null

                $script:startMenuCheck = New-Object System.Windows.Forms.CheckBox
                $script:startMenuCheck.Text = "Create Start Menu folder and shortcut"
                $script:startMenuCheck.Location = New-Object System.Drawing.Point(0, 58)
                $script:startMenuCheck.Size = New-Object System.Drawing.Size(420, 28)
                $script:startMenuCheck.Checked = $state.CreateStartMenu
                $panel.Controls.Add($script:startMenuCheck)

                $script:desktopCheck = New-Object System.Windows.Forms.CheckBox
                $script:desktopCheck.Text = "Create desktop shortcut"
                $script:desktopCheck.Location = New-Object System.Drawing.Point(0, 96)
                $script:desktopCheck.Size = New-Object System.Drawing.Size(420, 28)
                $script:desktopCheck.Checked = $state.CreateDesktopShortcut
                $panel.Controls.Add($script:desktopCheck)

                $script:launchCheck = New-Object System.Windows.Forms.CheckBox
                $script:launchCheck.Text = "Launch Light Host Modern after installation"
                $script:launchCheck.Location = New-Object System.Drawing.Point(0, 134)
                $script:launchCheck.Size = New-Object System.Drawing.Size(420, 28)
                $script:launchCheck.Checked = $state.LaunchAfterInstall
                $panel.Controls.Add($script:launchCheck)

                $nextButton.Text = "Next"
            }
            3 {
                $title.Text = "Ready to install"
                Add-BodyLabel "Light Host Modern will be installed with these settings:" 8 28 | Out-Null
                Add-BodyLabel "Install folder: $($state.InstallRoot)" 52 28 | Out-Null
                Add-BodyLabel "Start Menu shortcut: $(if ($state.CreateStartMenu) { 'Yes' } else { 'No' })" 88 28 | Out-Null
                Add-BodyLabel "Desktop shortcut: $(if ($state.CreateDesktopShortcut) { 'Yes' } else { 'No' })" 124 28 | Out-Null
                Add-BodyLabel "Launch after install: $(if ($state.LaunchAfterInstall) { 'Yes' } else { 'No' })" 160 28 | Out-Null
                $nextButton.Text = "Install"
            }
            4 {
                $title.Text = "Installation complete"
                Add-BodyLabel "Light Host Modern was installed successfully." 10 32 | Out-Null
                Add-BodyLabel "Installed to: $($state.InstalledPath)" 58 48 | Out-Null
                $backButton.Visible = $false
                $cancelButton.Visible = $false
                $nextButton.Text = "Finish"
            }
        }
    }

    $backButton.Add_Click({
        if ($state.Page -gt 0) {
            $state.Page--
            Render-Page
        }
    })

    $nextButton.Add_Click({
        try {
            switch ($state.Page) {
                0 { $state.Page = 1 }
                1 {
                    $candidate = $script:pathBox.Text.Trim()
                    if ([string]::IsNullOrWhiteSpace($candidate)) {
                        [System.Windows.Forms.MessageBox]::Show($form, "Choose an install location.", "Light Host Modern Setup", [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Warning) | Out-Null
                        return
                    }
                    $state.InstallRoot = $candidate
                    $state.Page = 2
                }
                2 {
                    $state.CreateStartMenu = $script:startMenuCheck.Checked
                    $state.CreateDesktopShortcut = $script:desktopCheck.Checked
                    $state.LaunchAfterInstall = $script:launchCheck.Checked
                    $state.Page = 3
                }
                3 {
                    $title.Text = "Installing"
                    $panel.Controls.Clear()
                    $script:statusLabel = Add-BodyLabel "Installing Light Host Modern..." 10 32
                    $backButton.Enabled = $false
                    $nextButton.Enabled = $false
                    $cancelButton.Enabled = $false
                    [System.Windows.Forms.Application]::DoEvents()
                    $state.InstalledPath = Install-LightHostModern -InstallRoot $state.InstallRoot -CreateStartMenu $state.CreateStartMenu -CreateDesktopShortcut $state.CreateDesktopShortcut -LaunchAfterInstall $state.LaunchAfterInstall
                    $state.Page = 4
                    $cancelButton.Enabled = $true
                }
                4 {
                    $form.DialogResult = [System.Windows.Forms.DialogResult]::OK
                    $form.Close()
                    return
                }
            }
            Render-Page
        }
        catch {
            $nextButton.Enabled = $true
            $backButton.Enabled = $state.Page -gt 0
            $cancelButton.Enabled = $true
            [System.Windows.Forms.MessageBox]::Show($form, $_.Exception.Message, "Light Host Modern Setup", [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
        }
    })

    $cancelButton.Add_Click({
        $form.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
        $form.Close()
    })

    Render-Page
    [void] $form.ShowDialog()
}

Show-InstallerWizard
'@ | Set-Content -LiteralPath (Join-Path $TargetDir "install.ps1") -Encoding UTF8
}

function Write-PortablePayload {
    param([Parameter(Mandatory)][string] $TargetDir)

    New-Directory -Path $TargetDir
    Copy-Item -LiteralPath $payloadZip -Destination (Join-Path $TargetDir "payload.zip") -Force

    @'
@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-portable.ps1"
exit /b %ERRORLEVEL%
'@ | Set-Content -LiteralPath (Join-Path $TargetDir "run-portable.cmd") -Encoding ASCII

    @'
$ErrorActionPreference = "Stop"

$exeName = "Light Host Modern.exe"
$payloadZip = Join-Path $PSScriptRoot "payload.zip"
$payloadHash = (Get-FileHash -LiteralPath $payloadZip -Algorithm SHA256).Hash.Substring(0, 16)
$portableRoot = Join-Path $env:TEMP "LightHostModern-Portable\$payloadHash"
$exePath = Join-Path $portableRoot $exeName

if (!(Test-Path -LiteralPath $exePath)) {
    if (Test-Path -LiteralPath $portableRoot) {
        Remove-Item -LiteralPath $portableRoot -Recurse -Force
    }

    New-Item -ItemType Directory -Force -Path $portableRoot | Out-Null
    Expand-Archive -LiteralPath $payloadZip -DestinationPath $portableRoot -Force
}

Start-Process -FilePath $exePath -WorkingDirectory $portableRoot
'@ | Set-Content -LiteralPath (Join-Path $TargetDir "run-portable.ps1") -Encoding UTF8
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

This software is licensed under the GNU General Public License version 3.

The installed application includes the full LICENSE file. The license is also available from the project repository.
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
            $fileId = New-WixFileId
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
    $manufacturer = "Light Host Modern"
    $escapedIconPath = ConvertTo-WixXmlText $IconPath
    $upgradeCode = "8F28E61C-DC90-4927-B7B4-3E74E4B5960B"
    $mainExeTarget = "[APPLICATIONFOLDER]$exeName"

    $wxs = New-Object System.Collections.Generic.List[string]
    $wxs.Add("<?xml version=`"1.0`" encoding=`"UTF-8`"?>")
    $wxs.Add("<Wix xmlns=`"http://wixtoolset.org/schemas/v4/wxs`" xmlns:ui=`"http://wixtoolset.org/schemas/v4/wxs/ui`">")
    $wxs.Add("  <Package Name=`"$productName`" Manufacturer=`"$manufacturer`" Version=`"$appVersion`" UpgradeCode=`"$upgradeCode`" Scope=`"perUserOrMachine`">")
    $wxs.Add("    <MajorUpgrade DowngradeErrorMessage=`"A newer version of $productName is already installed.`" />")
    $wxs.Add("    <MediaTemplate EmbedCab=`"yes`" />")
    $wxs.Add("    <Icon Id=`"AppIcon.ico`" SourceFile=`"$escapedIconPath`" />")
    $wxs.Add("    <Property Id=`"ARPPRODUCTICON`" Value=`"AppIcon.ico`" />")
    $wxs.Add("    <Property Id=`"ApplicationFolderName`" Value=`"$productName`" />")
    $wxs.Add("    <Property Id=`"WixAppFolder`" Value=`"WixPerMachineFolder`" />")
    $wxs.Add("    <WixVariable Id=`"WixUILicenseRtf`" Value=`"$licenseRtf`" />")
    $wxs.Add("    <ui:WixUI Id=`"WixUI_Advanced`" />")
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

    & $wix extension add $wixExtensionPackage | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to install or enable the WiX UI extension."
    }

    & $wix build $wxsPath -ext WixToolset.UI.wixext -arch x64 -o $TargetMsi | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "WiX failed to build the MSI installer."
    }

    $wixPdb = [System.IO.Path]::ChangeExtension($TargetMsi, ".wixpdb")
    if (Test-Path -LiteralPath $wixPdb) {
        Remove-Item -LiteralPath $wixPdb -Force
    }
}

if (!$SkipBuild) {
    $msbuild = Resolve-MSBuild
    $winUIProject = Join-Path $repoRoot "WinUI\LightHost.WinUI\LightHost.WinUI.vcxproj"

    Invoke-Checked -FilePath $msbuild -Arguments @(
        $winUIProject,
        "/restore",
        "/m",
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform"
    )

    $cmakePath = Resolve-CMake
    $configureArgs = @(
        "--preset", $Preset,
        "-DLIGHTHOST_ENABLE_VST2=$EnableVst2",
        "-DLIGHTHOST_VST2_PROVIDER=$Vst2Provider"
    )

    if (![string]::IsNullOrWhiteSpace($Vst2SdkDir)) {
        $configureArgs += "-DLIGHTHOST_VST2_SDK_DIR=$Vst2SdkDir"
    } elseif (![string]::IsNullOrWhiteSpace($env:LIGHTHOST_VST2_SDK_DIR)) {
        $configureArgs += "-DLIGHTHOST_VST2_SDK_DIR=$env:LIGHTHOST_VST2_SDK_DIR"
    }

    Invoke-Checked -FilePath $cmakePath -Arguments $configureArgs
    Invoke-Checked -FilePath $cmakePath -Arguments @("--build", "--preset", "$Preset-$($Configuration.ToLowerInvariant())", "--config", $Configuration)
}

$hostOutput = Join-Path $repoRoot "out\build\windows-vs2022\LightHost_artefacts\$Configuration"
$hostExe = Join-Path $hostOutput $exeName
$winUIOutput = Join-Path $hostOutput "WinUI\$Platform\$Configuration\LightHost.WinUI\LightHostWinUI.exe"

if (!(Test-Path -LiteralPath $hostExe)) {
    throw "Host executable was not found: $hostExe"
}

if (!(Test-Path -LiteralPath $winUIOutput)) {
    throw "WinUI executable was not found inside the host output. Build the WinUI project before building the host: $winUIOutput"
}

New-Directory -Path $stageRoot
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
Copy-Item -Path (Join-Path $hostOutput "*") -Destination $stageRoot -Recurse -Force

Get-ChildItem -LiteralPath $stageRoot -Recurse -File |
    Where-Object { $_.Extension -in @(".pdb", ".ilk", ".exp", ".lib", ".appxsym") } |
    Remove-Item -Force

Copy-VCRuntime -Destination $stageRoot

$releaseInfo = [ordered]@{
    name = $appName
    version = $appVersion
    configuration = $Configuration
    platform = $Platform
    builtAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    entryPoint = $exeName
}

$releaseInfo | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stageRoot "release-info.json") -Encoding UTF8

if (Test-Path -LiteralPath $payloadZip) {
    Remove-Item -LiteralPath $payloadZip -Force
}

Compress-Archive -Path (Join-Path $stageRoot "*") -DestinationPath $payloadZip -Force

$installerWork = Join-Path $packageWorkRoot "installer"
$portableWork = Join-Path $packageWorkRoot "portable"
Write-PortablePayload -TargetDir $portableWork

$legacyInstallerExe = Join-Path $outRoot "LightHostModern-Setup.exe"
if (Test-Path -LiteralPath $legacyInstallerExe) {
    try {
        Remove-Item -LiteralPath $legacyInstallerExe -Force
    } catch {
        Write-Warning "Could not remove legacy setup executable '$legacyInstallerExe': $($_.Exception.Message)"
    }
}

New-WixMsiPackage -SourceDir $stageRoot -WorkDir $installerWork -TargetMsi $installerMsi -IconPath $releaseIcon
New-NativeSelfExtractPackage -Name "Light Host Modern Portable" -WorkDir $portableWork -PayloadZipPath (Join-Path $portableWork "payload.zip") -EntryScriptPath (Join-Path $portableWork "run-portable.ps1") -TargetExe $portableExe -IconPath $releaseIcon

if (!$KeepStage) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
    Remove-Item -LiteralPath $packageWorkRoot -Recurse -Force
    Remove-Item -LiteralPath $payloadZip -Force
}

Write-Host ""
Write-Host "Release artifacts created:"
Write-Host "  Installer: $installerMsi"
Write-Host "  Portable:  $portableExe"
