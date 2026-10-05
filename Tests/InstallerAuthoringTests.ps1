# Builds a tiny, non-runnable MSI fixture using the production authoring function.
# Never installs it or executes its custom action. Lifecycle validation stays in VM.
param([string]$Configuration='Release')
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDirectory=Join-Path $repoRoot 'out/build/windows-vs2022'
$appName='LightHostModern'; $exeName='LightHostModern.exe'; $appVersion='2.0.0'
$tokens=$null; $parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repoRoot 'Utilities/Build Release.ps1'),[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
$names=@('Resolve-Wix','ConvertTo-WixXmlText','ConvertTo-RtfText','New-StableGuid','Assert-BuildOutputPath','New-Directory','New-WixMsiPackage')
foreach ($definition in $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$true)) {
    if ($names -contains $definition.Name) { . ([scriptblock]::Create($definition.Extent.Text)) }
}
$root=Assert-BuildOutputPath (Join-Path $repoRoot ('out/installer-authoring-'+[guid]::NewGuid().ToString('N')))
$payload=Join-Path $root 'payload'
New-Item -ItemType Directory -Path $payload -Force | Out-Null
foreach ($name in @('LightHostModern.exe','LightHostModernUpdateHelper.exe')) {
    [IO.File]::WriteAllText((Join-Path $payload $name),'Non-runnable authoring fixture; never install.')
}
$package=Join-Path $root 'fixture-do-not-install.msi'
New-WixMsiPackage -SourceDir $payload -WorkDir (Join-Path $root 'work') -TargetMsi $package -IconPath (Join-Path $repoRoot 'Icon/logo.ico')
$installer=New-Object -ComObject WindowsInstaller.Installer
$database=$null
try {
    $database=$installer.OpenDatabase($package,0)
    function Cell([string]$Query,[int]$Column=1) {
        $view=$database.OpenView($Query)
        try { [void]$view.Execute(); $record=$view.Fetch(); if (!$record) { throw "Missing MSI row: $Query" }; return $record.StringData($Column) }
        finally { [void]$view.Close(); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view) }
    }
    foreach ($table in 'InstallUISequence','InstallExecuteSequence') {
        $action=[int](Cell ('SELECT `Sequence` FROM `'+$table+'` WHERE `Action`=''PreserveInstallLocation'''))
        $cost=[int](Cell ('SELECT `Sequence` FROM `'+$table+'` WHERE `Action`=''CostInitialize'''))
        $related=[int](Cell ('SELECT `Sequence` FROM `'+$table+'` WHERE `Action`=''FindRelatedProducts'''))
        if ($action -le $related -or $action -ge $cost) { throw 'Location must be recovered after product discovery and before directory costing.' }
    }
    if ((Cell 'SELECT `Target` FROM `CustomAction` WHERE `Action`=''PreserveInstallLocation''') -ne 'PreserveInstallLocation') { throw 'Invalid custom action export.' }
    if ((Cell 'SELECT `Source` FROM `CustomAction` WHERE `Action`=''PreserveInstallLocation''') -ne 'InstallerActions') { throw 'Directory lookup must use the embedded DLL before files are installed.' }
    if ((Cell 'SELECT `Value` FROM `Property` WHERE `Property`=''SecureCustomProperties''') -notmatch '(^|;)APPLICATIONFOLDER(;|$)') { throw 'Selected directory cannot cross to the elevated installer.' }
    Write-Output "PASS production MSI location authoring (not installed): $package"
} finally {
    if ($database) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database) }
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer)
}
