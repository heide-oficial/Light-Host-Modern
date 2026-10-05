param([Parameter(Mandatory)][string] $PackageDirectory,
      [string] $ExpectedVersion = '2.0.0')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$root = [IO.Path]::GetFullPath($PackageDirectory)
if (!$root.StartsWith((Join-Path $repo 'out') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Package inspection requires a workspace output directory.' }
$metadata = Get-Content -LiteralPath (Join-Path $root 'release-artifacts.json') -Raw | ConvertFrom-Json
if ($ExpectedVersion -notmatch '^\d+\.\d+\.\d+$') { throw 'ExpectedVersion must contain three numeric parts.' }
$installerName = "LightHostModern-$ExpectedVersion-Setup.msi"
$portableName = "LightHostModern-v$ExpectedVersion-Portable.zip"
$expectedArtifacts = [ordered]@{ installed = $installerName; portable = $portableName }
$results = [Collections.Generic.List[object]]::new()
function Assert([bool] $Condition, [string] $Message) { if (!$Condition) { throw $Message } }
function Scenario([string] $Name, [scriptblock] $Work) {
    try { & $Work; $results.Add([ordered]@{ name = $Name; status = 'passed' }) }
    catch { $results.Add([ordered]@{ name = $Name; status = 'failed'; error = $_.Exception.Message }) }
}
Scenario 'Exactly two versioned artifacts match their published identity, size and SHA-256 metadata' {
    Assert ($metadata.formatVersion -eq 1 -and @($metadata.artifacts).Count -eq 2) 'Expected exactly one versioned MSI and one versioned portable ZIP'
    foreach ($kind in $expectedArtifacts.Keys) {
        $name = $expectedArtifacts[$kind]
        $matching = @($metadata.artifacts | Where-Object { $_.name -ceq $name -and $_.distribution -ceq $kind })
        Assert ($matching.Count -eq 1) "Missing, duplicated or incorrectly named $kind artifact: $name"
        $artifact = $matching[0]
        $file = Get-Item -LiteralPath (Join-Path $root $name)
        Assert ($file.Length -eq $artifact.size -and $artifact.digest -eq ('sha256:' + (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant())) 'Artifact size or digest mismatch'
        Assert ($artifact.version -eq $ExpectedVersion -and $artifact.architecture -eq 'x64') 'Unexpected version or architecture'
    }
}
Scenario 'Versioned portable includes verified launcher, host, worker, WinUI, scanner and helper without fixtures' {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $root $portableName))
    try {
        $names = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        function Read-ZipText([string]$Path) {
            $entry=$zip.Entries|Where-Object {$_.FullName.Replace('\','/') -eq $Path}
            Assert ($null -ne $entry) "Missing ZIP entry: $Path"
            $reader=[IO.StreamReader]::new($entry.Open())
            try{return $reader.ReadToEnd()}finally{$reader.Dispose()}
        }
        $layout=Read-ZipText 'portable-layout.json'|ConvertFrom-Json
        Assert ($layout.formatVersion -eq 1 -and $layout.launcherVersion -eq 1 -and $layout.initial.id.StartsWith($ExpectedVersion+'-')) 'Invalid portable layout'
        $prefix='versions/'+$layout.initial.id+'/'
        Assert ($names -contains 'LightHostModern.exe') 'Stable root launcher missing'
        foreach ($name in 'LightHostModern.exe', 'LightHostModernWorker.exe', 'LightHostModernScanner.exe', 'LightHostModernUpdateHelper.exe', 'WinUI/x64/Release/LightHostModern.WinUI/LightHostModernWinUI.exe', 'release-info.json', 'LICENSE', 'THIRD-PARTY-NOTICES.txt') {
            Assert ($names -contains ($prefix+$name)) "Missing versioned payload: $name"
        }
        $inventoryText=Read-ZipText ($prefix+'payload-manifest.json')
        $sha=[Security.Cryptography.SHA256]::Create()
        try {
            $digest=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($inventoryText)))).Replace('-','').ToLowerInvariant()
            Assert ($digest -eq $layout.initial.inventoryHash) 'Initial inventory digest mismatch'
            $inventory=$inventoryText|ConvertFrom-Json
            Assert (@($inventory.files | Where-Object path -eq 'THIRD-PARTY-NOTICES.txt').Count -eq 1) 'Notices missing from authenticated inventory'
            Assert (@($inventory.files | Where-Object { $_.path -match '^Licenses/.+\.(txt|md)$' }).Count -gt 0) 'Dependency licenses missing from authenticated inventory'
            foreach($file in $inventory.files){
                $entry=$zip.Entries|Where-Object {$_.FullName.Replace('\','/') -eq ($prefix+$file.path)}
                Assert ($entry -and $entry.Length -eq $file.size) "Payload size mismatch: $($file.path)"
                $stream=$entry.Open()
                try{$digest=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}finally{$stream.Dispose()}
                Assert ($digest -eq $file.sha256) "Payload hash mismatch: $($file.path)"
            }
        }finally{$sha.Dispose()}
        Assert (@($names | Where-Object { $_ -match '(?i)Dragonfly|LightHostModern[^/]*Tests|(^|/)(Tests|fixtures|test-profiles|obj|AppX)/|\.(vst3|clap)$' }).Count -eq 0) 'Test material or duplicate output in portable payload'
        Assert (@($names | Where-Object { $_ -match '(?i)\.(pfx|p12|key|pem|pvk|snk|appxrecipe|pdb)$|BaselineFixture' }).Count -eq 0) 'Private key material, build metadata or helper fixture in portable payload'
        $release = Read-ZipText ($prefix+'release-info.json') | ConvertFrom-Json
        Assert ($release.version -eq $ExpectedVersion -and $release.platform -eq 'x64' -and $release.entryPoint -eq 'LightHostModern.exe') 'Unexpected portable manifest'
        $legacyFiles = Read-ZipText ($prefix+'legacy-payload-files.json') | ConvertFrom-Json
        Assert ($legacyFiles -contains 'Uninstall-LightHostModern.ps1') 'Legacy installer-owned uninstaller would be left behind'
    } finally { $zip.Dispose() }
}
Scenario 'MSI preserves machine scope, upgrade identity, shortcuts and legacy migration' {
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $database = $installer.OpenDatabase((Join-Path $root $installerName), 0)
    function Rows([string] $Query) {
        $view = $database.OpenView($Query); [void] $view.Execute()
        try { while ($record = $view.Fetch()) { $record.StringData(1) } } finally { [void] $view.Close() }
    }
    Assert ((Rows "SELECT ``Value`` FROM ``Property`` WHERE ``Property``='ProductVersion'") -eq $ExpectedVersion) 'Unexpected MSI version'
    Assert ((Rows "SELECT ``Value`` FROM ``Property`` WHERE ``Property``='ALLUSERS'") -eq '1') 'MSI scope changed'
    Assert ((Rows "SELECT ``Value`` FROM ``Property`` WHERE ``Property``='UpgradeCode'") -eq '{8F28E61C-DC90-4927-B7B4-3E74E4B5960B}') 'MSI upgrade identity changed'
    $features = @(Rows 'SELECT `Feature` FROM `Feature`')
    $files = @(Rows 'SELECT `FileName` FROM `File`')
    Assert (@($files|Where-Object {$_ -match '(^|\|)LightHostModernWorker.exe$'}).Count -eq 1) 'Worker missing from MSI'
    Assert (@($files|Where-Object {$_ -match '(^|\|)THIRD-PARTY-NOTICES.txt$'}).Count -eq 1) 'Notices missing from MSI'
    Assert (@($files|Where-Object {$_ -match '(?i)\.(pfx|p12|key|pem|pvk|snk|appxrecipe|pdb)$|BaselineFixture'}).Count -eq 0) 'Private key material, build metadata or helper fixture in MSI'
    Assert ($features -contains 'StartMenuShortcutFeature' -and $features -contains 'DesktopShortcutFeature') 'Shortcut features changed'
    $components = @(Rows 'SELECT `Component` FROM `Component`')
    Assert ($components -notcontains 'LegacyInstallCleanupComponent') 'Unsafe recursive legacy cleanup remains'
    $actions = @(Rows 'SELECT `Action` FROM `CustomAction`')
    Assert ($actions -contains 'MigrateLegacyPayload') 'Verified post-commit legacy migration is missing'
    Assert ($actions -contains 'SetARPINSTALLLOCATION') 'MSI installation location is not recorded'
    $summary = $database.SummaryInformation(0)
    Assert ($summary.Property(7).StartsWith('x64;')) 'MSI architecture mismatch'
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($summary)
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database)
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer)
}
Scenario 'The actual update helper accepts signed packages and refuses unsigned automatic installation' {
    $helper=Join-Path $repo 'out/build/windows-vs2022/LightHostModern_artefacts/Release/LightHostModernUpdateHelper.exe'
    foreach($kind in @('portable','installed')){
        $name=$expectedArtifacts[$kind]
        $matching=@($metadata.artifacts|Where-Object { $_.name -ceq $name -and $_.distribution -ceq $kind })
        Assert ($matching.Count -eq 1) "Expected one $kind artifact for helper validation"
        $artifact=$matching[0]
        $arguments=@('--mode','validate','--operation',('"'+$root+'"'),'--package',('"'+(Join-Path $root $name)+'"'),
            '--distribution',$kind,'--version',('v'+$ExpectedVersion),'--size',[string]$artifact.size,'--sha256',$artifact.digest)
        $p=Start-Process $helper -ArgumentList $arguments -WindowStyle Hidden -PassThru
        $null=$p.Handle
        Assert ($p.WaitForExit(60000)) 'Helper validation timed out'
        Assert ($p.ExitCode -eq 0) "Real helper rejected $kind package"
        $signed=Test-Path -LiteralPath (Join-Path $root 'update-manifest.sig')
        $arguments[1]='validate-signed'
        $p=Start-Process $helper -ArgumentList $arguments -WindowStyle Hidden -PassThru
        $null=$p.Handle
        Assert ($p.WaitForExit(60000)) 'Signature validation timed out'
        Assert (($p.ExitCode -eq 0) -eq $signed) 'Signed package rejected or unsigned package accepted for automatic installation'
    }
}
Scenario 'Staging rejects a stale WinUI record before touching its destination' {
    $stageScript = Join-Path $repo 'Utilities\WinUI Output.ps1'
    $source = & $stageScript -Mode Resolve -Platform x64 -Configuration Release
    $stampPath = Join-Path $source 'lighthost-build.json'
    $original = [IO.File]::ReadAllBytes($stampPath)
    $destination = Join-Path $root 'stale-stage-test'
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $sentinel = Join-Path $destination 'sentinel.txt'
    [IO.File]::WriteAllText($sentinel, 'keep')
    try {
        $stamp = Get-Content -LiteralPath $stampPath -Raw | ConvertFrom-Json
        $stamp.sourceHash = 'not-current'
        $stamp | ConvertTo-Json | Set-Content -LiteralPath $stampPath -Encoding UTF8
        $rejected = $false
        try { & $stageScript -Mode Stage -Configuration Release -Platform x64 -Destination $destination }
        catch { $rejected = $_.Exception.Message -like '*does not match current sources*' }
        Assert ($rejected -and [IO.File]::ReadAllText($sentinel) -eq 'keep') 'Stale WinUI was staged or destination was changed before validation'
    } finally { [IO.File]::WriteAllBytes($stampPath, $original) }
}
Scenario 'Production portable engine prepares and applies the real ZIP with transient test-only trust' {
    $runner=Join-Path $repo 'out/build/windows-vs2022/Release/LightHostModernUpdateTests.exe'
    $p=Start-Process $runner -ArgumentList ('"'+(Join-Path $root $portableName)+'"') -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'real-portable-apply.log') -RedirectStandardError (Join-Path $root 'real-portable-apply.error.log')
    $null=$p.Handle
    Assert ($p.WaitForExit(180000)) 'Actual ZIP prepare/apply/rollback timed out; inspect the test process and log before continuing'
    Assert ($p.ExitCode -eq 0) 'Actual ZIP failed prepare/apply/rollback; see real-portable-apply.log'
}
$results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'package-inspection-results.json') -Encoding UTF8
$results | Format-Table -AutoSize
if (@($results | Where-Object { $_.status -eq 'failed' }).Count) { exit 1 }
