# Execute only inside the disposable Windows machine named by the caller.
# Without -Execute, this script inspects packages and writes the reviewable plan.
param([Parameter(Mandatory)][string]$CurrentMsi,
      [string]$PreviousMsi='',
      [string]$ExpectedVersion='2.0.0',
      [string]$OutputDirectory='out/msi-lifecycle',
      [switch]$Execute,
      [string]$DisposableComputerName='',
      [string]$InstallDirectory='',
      [switch]$CleanupPreferenceFixture)
$ErrorActionPreference='Stop'
$expectedUpgrade='{8F28E61C-DC90-4927-B7B4-3E74E4B5960B}'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$outputRoot=(Resolve-Path -LiteralPath $OutputDirectory).Path
$installer=New-Object -ComObject WindowsInstaller.Installer
$results=[Collections.Generic.List[object]]::new()
$installedByTest=[Collections.Generic.List[string]]::new()
function Assert-NoReparsePath([string]$Path) {
    $cursor=[IO.Path]::GetFullPath($Path)
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point is not allowed in a validation path: $cursor" }
        }
        $parent=[IO.Path]::GetDirectoryName($cursor)
        if ($parent -eq $cursor) { break }
        $cursor=$parent
    }
}
function Expected-InstallRoot([string]$Version) {
    $programFiles=[IO.Path]::GetFullPath($env:ProgramFiles).TrimEnd('\')
    if ($InstallDirectory) {
        if ($InstallDirectory -notmatch '^[A-Za-z]:[\\/]' -or $InstallDirectory.Contains('"') -or $InstallDirectory.Substring(2).Contains(':')) { throw 'Use an absolute local custom install directory without quotes or streams.' }
        $target=[IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
        if (!$target.StartsWith($programFiles+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'The custom validation directory must be below Program Files.' }
    } else {
        $productFolder=if ([version]$Version -lt [version]'1.4.0') { 'Light Host Modern' } else { 'LightHostModern' }
        $target=Join-Path $programFiles $productFolder
    }
    Assert-NoReparsePath $target
    $target
}
function Remove-OwnedPreferenceFixture([string]$Path,[string]$ExpectedHash) {
    if (!$Path -or !$ExpectedHash) { throw 'Preference cleanup requires the exact fixture path and hash.' }
    Assert-NoReparsePath $Path
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw 'Preference fixture disappeared before cleanup.' }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $ExpectedHash) { throw 'Preference fixture changed; retained for review instead of deleting it.' }
    Remove-Item -LiteralPath $Path
}
function Package([string]$Path) {
    $file=Get-Item -LiteralPath $Path
    if ($file.Extension -ne '.msi') { throw 'An MSI package is required.' }
    $database=$installer.OpenDatabase($file.FullName,0)
    try {
        $values=@{path=$file.FullName;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
        foreach ($key in @('ProductCode','ProductVersion','UpgradeCode','ALLUSERS')) {
            $view=$database.OpenView(('SELECT `Value` FROM `Property` WHERE `Property`='''+$key+''''))
            try { [void]$view.Execute(); $record=$view.Fetch(); $values[$key]=$record.StringData(1) }
            finally { [void]$view.Close(); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view) }
        }
        $summary=$database.SummaryInformation(0)
        try { $values.architecture=$summary.Property(7) } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($summary) }
        if ($values.UpgradeCode -ne $expectedUpgrade -or $values.ALLUSERS -ne '1' -or !$values.architecture.StartsWith('x64;')) { throw 'Package identity, scope or architecture does not match LightHostModern.' }
        [pscustomobject]$values
    } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database) }
}
function Scenario([string]$Name,[scriptblock]$Action) {
    try { & $Action; $results.Add([pscustomobject]@{name=$Name;status='passed'}); Write-Host "PASS: $Name" }
    catch { $results.Add([pscustomobject]@{name=$Name;status='failed';error=$_.Exception.Message}); throw }
}
function Msi([string]$Name,[string[]]$Arguments) {
    $log=Join-Path $outputRoot ($Name+'.log')
    $process=Start-Process -FilePath "$env:SystemRoot\System32\msiexec.exe" -ArgumentList ($Arguments+@('/qn','/norestart','/L*v',('"'+$log+'"'))) -WindowStyle Hidden -PassThru
    $null=$process.Handle
    if (!$process.WaitForExit(180000)) { $script:installerStillRunning=$true; throw "Installer $Name (PID $($process.Id)) is still running; inspect $log before continuing. It was not forcibly stopped." }
    $script:installerOutcomes.Add([pscustomobject]@{stage=$Name;exitCode=$process.ExitCode;rebootRequired=($process.ExitCode -in @(1641,3010));log=$log})
    if ($process.ExitCode -notin @(0,1641,3010)) { throw "Installer $Name failed with $($process.ExitCode); see $log." }
    if ($process.ExitCode -eq 1641) { throw 'The installer initiated a restart despite /norestart; the remaining scenarios require a new run.' }
}
function InstalledRoot([string]$Code) {
    if ($installer.ProductState($Code) -ne 5) { throw "The product is not installed: $Code" }
    $path=[IO.Path]::GetFullPath($installer.ProductInfo($Code,'InstallLocation')).TrimEnd('\')
    $allowed=Expected-InstallRoot ($installer.ProductInfo($Code,'VersionString'))
    if (!$path.Equals($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw "Unexpected installation directory: $path" }
    $path
}
function Current-ShortcutPaths {
    (Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'LightHostModern\LightHostModern.lnk')
    (Join-Path ([Environment]::GetFolderPath('CommonDesktopDirectory')) 'LightHostModern.lnk')
}
function Installed-PayloadInventory([string]$Directory) {
    $root=[IO.Path]::GetFullPath($Directory).TrimEnd('\')
    Assert-NoReparsePath $root
    $pending=[Collections.Generic.Stack[string]]::new()
    $files=[Collections.Generic.List[string]]::new()
    $pending.Push($root)
    while ($pending.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            $path=[IO.Path]::GetFullPath($item.FullName)
            if (!$path.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or
                ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Installed payload inventory contains an unsafe path.' }
            if ($item.PSIsContainer) { $pending.Push($path) }
            else { $files.Add($path) }
        }
    }
    if (!$files.Count) { throw 'The installed payload inventory is empty.' }
    $files | Sort-Object
}
function Verify-Uninstalled([string]$Code,[string[]]$InstalledFiles) {
    if (!$InstalledFiles.Count) { throw 'Uninstall verification requires the captured installed payload inventory.' }
    if ($installer.ProductState($Code) -ne -1) { throw 'Uninstall left product registration.' }
    foreach ($path in $InstalledFiles) {
        if (Test-Path -LiteralPath $path) { throw "Uninstall left an installed payload file: $path" }
    }
    foreach ($path in Current-ShortcutPaths) {
        if (Test-Path -LiteralPath $path) { throw "Uninstall left an application shortcut: $path" }
    }
}
function Invoke-InstalledStartupSmoke([string]$Directory) {
    $profileName='msi-smoke-'+[guid]::NewGuid().ToString('N')
    $profileRoot=[IO.Path]::GetFullPath((Join-Path $outputRoot 'profiles'))
    $profileDirectory=Join-Path $profileRoot $profileName
    Assert-NoReparsePath $profileDirectory
    if (Test-Path -LiteralPath $profileDirectory) { throw 'The isolated startup profile already exists.' }
    New-Item -ItemType Directory -Path $profileDirectory -Force | Out-Null
    $hostExecutable=Join-Path $Directory 'LightHostModern.exe'
    $uiDirectory=Join-Path $Directory 'WinUI\x64\Release\LightHostModern.WinUI'
    $uiExecutable=Join-Path $uiDirectory 'LightHostModernWinUI.exe'
    $evidencePath=Join-Path $profileDirectory 'startup-smoke.json'
    $owned=[Collections.Generic.List[object]]::new()
    $smoke=[ordered]@{status='running';profile=$profileName;directory=$profileDirectory;error='';
        host=$null;ui=$null;hello=$null;snapshot=$null;nativeDependencies=@();normalExit=$false;cleanup=@()}
    $hostProcess=$null;$hostIdentity=$null;$uiIdentity=$null;$pipe='';$session=''
    $profileArgument='(?i)(?:^|\s)"?--test-profile='+[regex]::Escape($profileName)+'(?:"|\s|$)'
    function Own-Process($Process,[string]$ExpectedPath,[int]$ParentId) {
        $null=$Process.Handle # Pin the exact process object before recording identity.
        $identity=[pscustomobject]@{process=$Process;pid=$Process.Id;path=$ExpectedPath;
            createdUtcTicks=$Process.StartTime.ToUniversalTime().Ticks;parentPid=$ParentId}
        $owned.Add($identity)
        $identity
    }
    function Assert-OwnedProcess($Identity) {
        if ($Identity.process.HasExited) { throw 'A smoke process exited unexpectedly.' }
        $currentProcess=Get-CimInstance Win32_Process -Filter ('ProcessId='+$Identity.pid)
        if (!$currentProcess -or !$currentProcess.ExecutablePath -or
            !([IO.Path]::GetFullPath($currentProcess.ExecutablePath)).Equals($Identity.path,[StringComparison]::OrdinalIgnoreCase) -or
            $currentProcess.CommandLine -notmatch $profileArgument -or
            ($Identity.parentPid -and $currentProcess.ParentProcessId -ne $Identity.parentPid)) { throw 'A smoke process identity no longer matches this run.' }
        $fresh=Get-Process -Id $Identity.pid
        try { if ($fresh.StartTime.ToUniversalTime().Ticks -ne $Identity.createdUtcTicks) { throw 'A smoke process ID was reused.' } }
        finally { $fresh.Dispose() }
    }
    function Find-OwnedUi {
        if (!$hostIdentity) { return }
        foreach ($candidate in @(Get-CimInstance Win32_Process -Filter "Name='LightHostModernWinUI.exe'")) {
            if ($candidate.ParentProcessId -ne $hostIdentity.pid -or !$candidate.ExecutablePath -or
                !([IO.Path]::GetFullPath($candidate.ExecutablePath)).Equals($uiExecutable,[StringComparison]::OrdinalIgnoreCase) -or
                $candidate.CommandLine -notmatch $profileArgument) { continue }
            $existing=@($owned | Where-Object { $_.pid -eq $candidate.ProcessId -and !$_.process.HasExited })
            if ($existing.Count) { $existing[0]; continue }
            $process=Get-Process -Id $candidate.ProcessId -ErrorAction SilentlyContinue
            if (!$process) { continue }
            if ($process.StartTime.ToUniversalTime().Ticks -lt $hostIdentity.createdUtcTicks) { $process.Dispose(); continue }
            Own-Process $process $uiExecutable $hostIdentity.pid
        }
    }
    try {
        foreach ($executable in @($hostExecutable,$uiExecutable)) {
            Assert-NoReparsePath $executable
            $version=(Get-Item -LiteralPath $executable).VersionInfo
            if ($version.ProductVersion -ne $ExpectedVersion -or $version.FileVersion -ne $ExpectedVersion) { throw 'Installed host/UI version does not match the candidate.' }
        }
        $dependencies=@((Join-Path $Directory 'vcruntime140.dll'),(Join-Path $Directory 'msvcp140.dll'),
            (Join-Path $uiDirectory 'Microsoft.WindowsAppRuntime.dll'),(Join-Path $uiDirectory 'Microsoft.ui.xaml.dll'))
        $smoke.nativeDependencies=@(foreach ($dependency in $dependencies) {
            Assert-NoReparsePath $dependency
            if (!(Test-Path -LiteralPath $dependency -PathType Leaf)) { throw "An app-local runtime dependency is missing: $dependency" }
            [pscustomobject]@{path=$dependency;version=(Get-Item -LiteralPath $dependency).VersionInfo.FileVersion;sha256=(Get-FileHash -LiteralPath $dependency).Hash}
        })
        $hostProcess=Start-Process -FilePath $hostExecutable -WorkingDirectory $Directory -ArgumentList @(
            ('--test-profile='+$profileName),('--profile-root="'+$profileRoot+'"'),'--no-audio','--show-ui') -WindowStyle Hidden -PassThru
        $hostIdentity=Own-Process $hostProcess $hostExecutable 0
        $smoke.host=$hostIdentity | Select-Object pid,path,createdUtcTicks,parentPid
        $deadline=[DateTime]::UtcNow.AddSeconds(45)
        $ready=$false
        do {
            Assert-OwnedProcess $hostIdentity
            $candidates=@(Find-OwnedUi)
            if ($candidates.Count -gt 1) { throw 'The installed host launched duplicate UI processes.' }
            if ($candidates.Count) { $uiIdentity=$candidates[0] }
            $metadataPath=Join-Path $profileDirectory 'profile.json'
            $info=$null
            if (Test-Path -LiteralPath $metadataPath) {
                try { $info=Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json } catch { }
            }
            if ($info -and $info.profile -eq $profileName -and $info.pid -eq $hostIdentity.pid -and
                $info.audioInitiallySuspended -eq $true -and $info.pipe -like '\\.\pipe\LightHostModern-profile-*' -and $uiIdentity) {
                Assert-OwnedProcess $uiIdentity
                $uiIdentity.process.Refresh()
                if ($uiIdentity.process.MainWindowHandle -ne [IntPtr]::Zero -and $uiIdentity.process.MainWindowTitle.Contains($profileName)) {
                    $pipe=$info.pipe
                    $hello=Send-HostRequest $pipe 'hello' -TimeoutMs 5000
                    $snapshot=Send-HostRequest $pipe 'snapshot' -TimeoutMs 5000
                    if ($hello.status -ne 'ok' -or !$hello.hostSession -or $snapshot.hostSession -ne $hello.hostSession -or
                        $snapshot.hostPid -ne $hostIdentity.pid -or $snapshot.hostExecutable -ine $hostExecutable -or $snapshot.status -ne 'online') { throw 'Installed host IPC did not identify the expected process/session.' }
                    if ($snapshot.audioSelection.driverAvailable -ne $false -or $snapshot.audioSelection.processingAvailable -ne $false -or
                        $snapshot.diagnostics.recoveryState -ne 'suspended') { throw 'The isolated startup smoke did not keep real audio suspended.' }
                    $session=$hello.hostSession
                    $smoke.hello=$hello;$smoke.snapshot=$snapshot
                    $smoke.ui=$uiIdentity | Select-Object pid,path,createdUtcTicks,parentPid
                    $smoke.ui | Add-Member -NotePropertyName windowHandle -NotePropertyValue $uiIdentity.process.MainWindowHandle.ToInt64()
                    $ready=$true;break
                }
            }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if (!$ready) { throw 'The installed host/UI did not become ready within 45 seconds.' }
        Assert-OwnedProcess $hostIdentity
        Send-HostRequest $pipe 'quit-host' -Session $session -TimeoutMs 5000 | Out-Null
        if (!$hostProcess.WaitForExit(15000) -or !$uiIdentity.process.WaitForExit(15000)) { throw 'The installed host/UI did not exit normally after quit-host.' }
        if ($hostProcess.ExitCode -ne 0 -or $uiIdentity.process.ExitCode -ne 0) { throw 'The installed host/UI returned an error on normal shutdown.' }
        $smoke.normalExit=$true;$smoke.status='passed'
    } catch { $smoke.status='failed';$smoke.error=$_.Exception.Message }
    finally {
        # Retain the test profile as evidence. Never remove user profiles or
        # enumerate/terminate processes by executable name alone.
        try { Find-OwnedUi | Out-Null } catch { $smoke.cleanup+=@($_.Exception.Message) }
        foreach ($identity in $owned) {
            try {
                if (!$identity.process.HasExited) {
                    Assert-OwnedProcess $identity
                    $identity.process.Kill()
                    if (!$identity.process.WaitForExit(5000)) { throw 'An owned smoke process did not stop during failure cleanup.' }
                    $smoke.cleanup+=@('Stopped owned PID '+$identity.pid)
                    $smoke.status='failed'
                    if (!$smoke.error) { $smoke.error='A smoke process required forced cleanup instead of normal shutdown.' }
                }
            } catch {
                $smoke.cleanup+=@($_.Exception.Message);$smoke.status='failed'
                if (!$identity.process.HasExited) { $script:startupProcessStillRunning=$true }
            }
            finally { $identity.process.Dispose() }
        }
        $smoke | ConvertTo-Json -Depth 24 | Set-Content -LiteralPath $evidencePath -Encoding UTF8
        $script:startupSmokes.Add([pscustomobject]@{status=$smoke.status;profile=$profileName;evidence=$evidencePath;normalExit=$smoke.normalExit})
    }
    if ($smoke.status -ne 'passed') { throw "Installed startup smoke failed: $($smoke.error). See $evidencePath" }
}
function Verify-Payload([string]$Code,[switch]$Current) {
    $directory=InstalledRoot $Code
    $modernNames=[version]$installer.ProductInfo($Code,'VersionString') -ge [version]'1.4.0'
    $payload=if ($modernNames) { @('LightHostModern.exe','WinUI\x64\Release\LightHostModern.WinUI\LightHostModernWinUI.exe') } else { @('Light Host Modern.exe','WinUI\x64\Release\LightHost.WinUI\LightHostWinUI.exe') }
    if ($modernNames) { $payload+=@('LightHostModernScanner.exe','LightHostModernUpdateHelper.exe') }
    if ([version]$installer.ProductInfo($Code,'VersionString') -ge [version]'2.0.0') { $payload+='LightHostModernWorker.exe' }
    foreach ($relative in $payload) {
        if (!(Test-Path -LiteralPath (Join-Path $directory $relative) -PathType Leaf)) { throw "Installed payload is missing $relative" }
    }
    if (!$Current) { return $directory }
    $shell=New-Object -ComObject WScript.Shell
    try {
        foreach ($relative in Current-ShortcutPaths) {
            if (!(Test-Path -LiteralPath $relative)) { throw "Installed shortcut is missing: $relative" }
            $shortcut=$shell.CreateShortcut($relative)
            try { if ($shortcut.TargetPath -ne (Join-Path $directory 'LightHostModern.exe')) { throw 'A shortcut targets an obsolete installation.' } }
            finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcut) }
        }
    } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell) }
    Invoke-InstalledStartupSmoke $directory
    $directory
}
$script:installerOutcomes=[Collections.Generic.List[object]]::new()
$script:installerStillRunning=$false
$script:startupProcessStillRunning=$false
$preferenceFixtureCreated=$false
$preferenceFixtureCleaned=$false
$prefs=''
$prefsHash=''
$script:payloadInventories=[ordered]@{clean=@();upgrade=@()}
$script:startupSmokes=[Collections.Generic.List[object]]::new()
try {
    $current=Package $CurrentMsi
    $previous=if ($PreviousMsi) { Package $PreviousMsi } else { $null }
    if ($current.ProductVersion -ne $ExpectedVersion) { throw "Expected package version $ExpectedVersion." }
    if ($previous -and ([version]$previous.ProductVersion -ge [version]$current.ProductVersion -or $previous.ProductCode -eq $current.ProductCode)) { throw 'Upgrade testing requires an earlier genuine product version with a different ProductCode.' }
    $expectedCurrentRoot=Expected-InstallRoot $current.ProductVersion
    $expectedPreviousRoot=if ($previous) { Expected-InstallRoot $previous.ProductVersion } else { '' }
    $plan=[pscustomobject]@{current=$current;previous=$previous;executed=[bool]$Execute;computer=$env:COMPUTERNAME;expectedDisposableComputer=$DisposableComputerName;installDirectory=$InstallDirectory;
        expectedCurrentRoot=$expectedCurrentRoot;expectedPreviousRoot=$expectedPreviousRoot;cleanupPreferenceFixture=[bool]$CleanupPreferenceFixture;
        stages=@('clean install current MSI and verify payload, version, shortcuts and isolated host/UI startup without audio','uninstall clean current installation and verify captured files and shortcuts are removed',
            'install previous version','seed preference preservation fixture','upgrade to current MSI','verify registered version, shortcuts and isolated host/UI startup without audio',
            'repair a removed scanner payload and repeat isolated host/UI startup','uninstall and verify captured files and shortcuts are removed','verify preserved preferences')}
    $plan | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $outputRoot 'plan.json') -Encoding UTF8
    if (!$Execute) { $plan | ConvertTo-Json -Depth 8; return }
    if (!$previous -or !$DisposableComputerName -or $env:COMPUTERNAME -ne $DisposableComputerName) { throw 'Execution requires an earlier MSI and the exact disposable Windows computer name.' }
    $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run the test from an elevated terminal inside the disposable Windows machine.' }
    if (![Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell for this x64 installer test.' }
    . (Join-Path $PSScriptRoot 'HostProtocol.ps1')
    if (@($installer.RelatedProducts($expectedUpgrade)).Count) { throw 'Use a fresh disposable machine without a registered LightHostModern installation.' }
    foreach ($target in @($expectedCurrentRoot,$expectedPreviousRoot) | Select-Object -Unique) {
        if ((Test-Path -LiteralPath $target) -and @(Get-ChildItem -LiteralPath $target -Force).Count) { throw "Validation requires an empty installation directory: $target" }
    }
    $prefs=Join-Path $env:APPDATA 'LightHostModern\LightHostModern.settings'
    Assert-NoReparsePath $prefs
    if (Test-Path -LiteralPath $prefs) { throw 'The disposable machine already contains application preferences.' }
    foreach ($shortcut in Current-ShortcutPaths) {
        Assert-NoReparsePath $shortcut
        if (Test-Path -LiteralPath $shortcut) { throw 'The disposable machine already contains application shortcuts.' }
    }
    Scenario 'Clean install the current MSI and verify registration, payload and shortcuts' {
        $installedByTest.Add($current.ProductCode)
        $arguments=@('/i',('"'+$current.path+'"'),'ADDLOCAL=ALL')
        if ($InstallDirectory) { $arguments += 'APPLICATIONFOLDER="' + $expectedCurrentRoot + '"' }
        Msi 'install-current-clean' $arguments
        $script:cleanInstallRoot=Verify-Payload $current.ProductCode -Current
        if ($installer.ProductInfo($current.ProductCode,'VersionString') -ne $current.ProductVersion) { throw 'The clean installation registered an incorrect version.' }
        $script:payloadInventories.clean=@(Installed-PayloadInventory $script:cleanInstallRoot)
    }
    Scenario 'Uninstall the clean current installation and verify all captured payload files and shortcuts are removed' {
        Msi 'uninstall-current-clean' @('/x',$current.ProductCode)
        Verify-Uninstalled $current.ProductCode $script:payloadInventories.clean
        if (Test-Path -LiteralPath $prefs) { throw 'The clean installer lifecycle unexpectedly created application preferences; retained for review.' }
    }
    Scenario 'Install the previous MSI with its existing machine scope and shortcuts' {
        $installedByTest.Add($previous.ProductCode)
        $arguments = @('/i',('"'+$previous.path+'"'),'ADDLOCAL=ALL')
        if ($InstallDirectory) {
            $arguments += 'APPLICATIONFOLDER="' + $expectedPreviousRoot + '"'
        }
        Msi 'install-previous' $arguments
        $script:previousInstallRoot = Verify-Payload $previous.ProductCode
    }
    $fixture='<PROPERTIES><VALUE name="installer-validation" val="'+[guid]::NewGuid().ToString('N')+'"/></PROPERTIES>'
    New-Item -ItemType Directory -Force -Path (Split-Path $prefs) | Out-Null
    Assert-NoReparsePath $prefs
    $fixtureBytes=[Text.UTF8Encoding]::new($false).GetBytes($fixture)
    $fixtureStream=[IO.File]::Open($prefs,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $fixtureStream.Write($fixtureBytes,0,$fixtureBytes.Length); $fixtureStream.Flush($true) }
    finally { $fixtureStream.Dispose() }
    $preferenceFixtureCreated=$true
    $prefsHash=(Get-FileHash -LiteralPath $prefs).Hash
    Scenario 'Upgrade registration, payload and shortcuts while preserving preferences' {
        Msi 'upgrade-current' @('/i',('"'+$current.path+'"'),'ADDLOCAL=ALL')
        $script:installRoot=Verify-Payload $current.ProductCode -Current
        if ($script:installRoot.TrimEnd('\') -ine $script:previousInstallRoot.TrimEnd('\')) { throw 'Upgrade moved the existing installation directory.' }
        if ($installer.ProductState($previous.ProductCode) -eq 5 -or $installer.ProductInfo($current.ProductCode,'VersionString') -ne $current.ProductVersion) { throw 'The upgrade left incorrect product registration.' }
        if ((Get-FileHash -LiteralPath $prefs).Hash -ne $prefsHash) { throw 'Upgrade changed user preferences.' }
        $script:payloadInventories.upgrade=@(Installed-PayloadInventory $script:installRoot)
    }
    Scenario 'Repair restores the exact current scanner payload' {
        $scanner=[IO.Path]::GetFullPath((Join-Path $script:installRoot 'LightHostModernScanner.exe'))
        $saved=[IO.Path]::GetFullPath((Join-Path $outputRoot ('repair-scanner-'+[guid]::NewGuid().ToString('N')+'.exe')))
        if (!$scanner.StartsWith($script:installRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or !$saved.StartsWith($outputRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Repair fixture paths escaped their verified directories.' }
        $before=(Get-FileHash -LiteralPath $scanner).Hash
        Move-Item -LiteralPath $scanner -Destination $saved
        Msi 'repair-current' @('/fa',$current.ProductCode)
        if ((Get-FileHash -LiteralPath $scanner).Hash -ne $before) { throw 'Repair restored an incorrect scanner.' }
        Verify-Payload $current.ProductCode -Current | Out-Null
    }
    Scenario 'Uninstall removes registration and application files and preserves preferences' {
        Msi 'uninstall-current' @('/x',$current.ProductCode)
        Verify-Uninstalled $current.ProductCode $script:payloadInventories.upgrade
        if ((Get-FileHash -LiteralPath $prefs).Hash -ne $prefsHash) { throw 'Uninstall changed user preferences.' }
    }
} finally {
    if ($Execute -and !$script:installerStillRunning -and !$script:startupProcessStillRunning) {
        # Clean up only product identities installed by this test. Never enumerate
        # unrelated products through Win32_Product, which can trigger repairs.
        foreach ($code in $installedByTest) {
            if ($script:installerStillRunning) { break }
            if ($installer.ProductState($code) -eq 5) {
                try { Msi ('cleanup-'+$code.Trim('{}')) @('/x',$code) }
                catch { $results.Add([pscustomobject]@{name='cleanup';status='failed';error=$_.Exception.Message}) }
            }
        }
        if ($CleanupPreferenceFixture -and $preferenceFixtureCreated -and !$script:installerStillRunning) {
            try {
                if (@($installer.RelatedProducts($expectedUpgrade)).Count) { throw 'An installed product remains; preference fixture retained for review.' }
                Remove-OwnedPreferenceFixture $prefs $prefsHash
                $preferenceFixtureCleaned=$true
                $results.Add([pscustomobject]@{name='Remove only the unchanged preference fixture created by this run';status='passed'})
            } catch { $results.Add([pscustomobject]@{name='preference fixture cleanup';status='failed';error=$_.Exception.Message}) }
        }
    }
    [pscustomobject]@{executed=[bool]$Execute;scenarios=@($results);installerOutcomes=@($script:installerOutcomes);installedPayloadInventories=$script:payloadInventories;startupSmokes=@($script:startupSmokes);startupProcessStillRunning=$script:startupProcessStillRunning;
        preferenceFixture=[pscustomobject]@{path=$prefs;sha256=$prefsHash;created=$preferenceFixtureCreated;cleaned=$preferenceFixtureCleaned}} |
        ConvertTo-Json -Depth 8 | Set-Content (Join-Path $outputRoot 'results.json') -Encoding UTF8
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer)
}
if (@($results | Where-Object status -eq 'failed').Count) { exit 1 }
