<#
.SYNOPSIS
Validates the Windows portable ZIP and an isolated installation, version upgrade, and uninstall.
.DESCRIPTION
Compile an older-version installer fixture with the current installer source and the same
portable payload, then pass that fixture as InstallerPath and the release as UpgradeInstallerPath.
The installer redirects its registry writes into a unique PackageValidation namespace;
application smoke runs redirect their settings and use a minimal Windows PATH.
The report, subprocess logs, regression JUnit, and native UI screenshots are kept in OutputDirectory.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $PortableArchive,
    [Parameter(Mandatory = $true)] [string] $BuildDirectory,
    [string] $InstallerPath,
    [string] $UpgradeInstallerPath,
    [string] $QtPrefix,
    [string] $TemporaryRoot,
    [string] $OutputDirectory,
    [int] $Jobs = 4,
    [switch] $KeepWorkDirectory
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Package-RegressionCoverage.ps1')
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Package validation requires PowerShell 7 or newer.' }
if (-not $IsWindows) { throw 'This script validates the Windows portable package and installer.' }
$taskArchive = (Resolve-Path -LiteralPath $PortableArchive).Path
$taskBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path
if (Test-Path -LiteralPath (Join-Path $taskBuild 'application/CMakeCache.txt')) {
    $taskBuild = Join-Path $taskBuild 'application'
}
if (-not $InstallerPath) { $InstallerPath = [IO.Path]::ChangeExtension($taskArchive, $null) + '-setup.exe' }
$taskInstaller = (Resolve-Path -LiteralPath $InstallerPath).Path
$taskUpgradeInstaller = if ($UpgradeInstallerPath) {
    (Resolve-Path -LiteralPath $UpgradeInstallerPath).Path
} else { (Resolve-Path -LiteralPath ([IO.Path]::ChangeExtension($taskArchive, $null) + '-setup.exe')).Path }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path (Split-Path $taskArchive -Parent) 'package-validation' }
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($taskOutput) | Out-Null
if (-not $TemporaryRoot) {
    $TemporaryRoot = if (Test-Path -LiteralPath 'D:/Cache/Codex') { 'D:/Cache/Codex' } else { [IO.Path]::GetTempPath() }
}
$taskTempRoot = [IO.Path]::GetFullPath($TemporaryRoot)
[IO.Directory]::CreateDirectory($taskTempRoot) | Out-Null
$taskId = [Guid]::NewGuid().ToString('D')
$taskWork = Join-Path $taskTempRoot ('AnAnArchiver-Package-' + $taskId)
$taskRegistryPrefix = 'Software\AnAnArchiver\PackageValidation\' + $taskId
$taskInstall = Join-Path $taskWork '安装位置 中文 空格'
while ($taskInstall.Length -lt 275) { $taskInstall = Join-Path $taskInstall '长路径 安装 验证 abcdefghijklmnopqrst' }
$taskInstall = Join-Path $taskInstall 'AnAnArchiver'
[IO.Directory]::CreateDirectory($taskWork) | Out-Null
$taskChecks = [Collections.Generic.List[object]]::new()
$taskProcesses = [Collections.Generic.List[object]]::new()
$taskCtest = (Get-Command ctest -ErrorAction Stop).Source
$taskReport = [ordered]@{
    schema = 1; status = 'running'; started_utc = [DateTime]::UtcNow.ToString('o')
    portable_archive = $taskArchive; installer = $taskInstaller; upgrade_installer = $taskUpgradeInstaller
    build_directory = $taskBuild; work_directory = $taskWork; registry_prefix = $taskRegistryPrefix
    checks = $taskChecks; processes = $taskProcesses
}

function Assert-Condition([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
}
function Complete-Check([string] $Name, $Evidence) {
    $taskChecks.Add([ordered]@{ name = $Name; status = 'passed'; evidence = $Evidence })
    Write-Host ('PASS ' + $Name)
}
function ConvertTo-NormalTestPath([string] $Path) {
    if ($Path.StartsWith('\\?\UNC\', [StringComparison]::OrdinalIgnoreCase)) { return '\\' + $Path.Substring(8) }
    if ($Path.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) { return $Path.Substring(4) }
    return $Path
}
function ConvertTo-NativeTestPath([string] $Path, [int] $MinimumLength = 241) {
    $taskNormal = ConvertTo-NormalTestPath $Path
    if ([IO.Path]::IsPathRooted($taskNormal) -and $taskNormal.Length -ge $MinimumLength) {
        if ($taskNormal.StartsWith('\\', [StringComparison]::Ordinal)) { return '\\?\UNC\' + $taskNormal.Substring(2) }
        return '\\?\' + $taskNormal
    }
    return $taskNormal
}
function Assert-Inside([string] $Path, [string] $Root) {
    $taskFull = [IO.Path]::GetFullPath((ConvertTo-NormalTestPath $Path))
    $taskRootFull = [IO.Path]::GetFullPath((ConvertTo-NormalTestPath $Root)).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    Assert-Condition ($taskFull.StartsWith($taskRootFull, [StringComparison]::OrdinalIgnoreCase)) ('Unsafe test path: ' + $taskFull)
    return $taskFull
}
function Invoke-TestProcess {
    param([string] $Executable, [string[]] $Arguments, [string] $Name,
          [string] $WorkingDirectory = $taskWork, [int] $TimeoutSeconds = 90,
          [switch] $Application, [switch] $ObserveQtModules, [string] $Platform = 'windows',
          [int] $ExpectedExitCode = 0, [switch] $ExpectFailure, [switch] $UseExactExecutable,
          [switch] $PoisonQtEnvironment)
    $taskNativeExe = if ($UseExactExecutable) { $Executable } else { ConvertTo-NativeTestPath $Executable }
    $taskInfo = [Diagnostics.ProcessStartInfo]::new($taskNativeExe)
    $taskInfo.UseShellExecute = $false
    $taskInfo.CreateNoWindow = $true
    $taskInfo.RedirectStandardOutput = $true
    $taskInfo.RedirectStandardError = $true
    $taskInfo.WorkingDirectory = $WorkingDirectory
    foreach ($taskArgument in $Arguments) { $taskInfo.ArgumentList.Add($taskArgument) }
    $taskInfo.Environment['TEMP'] = $taskWork
    $taskInfo.Environment['TMP'] = $taskWork
    $taskInfo.Environment['ANANARCHIVER_TEST_TEMP_ROOT'] = $taskWork
    $taskInfo.Environment['ANANARCHIVER_SETTINGS_FILE'] = Join-Path $taskWork 'settings.ini'
    if ($Application) {
        # Native QSettings is redirected by the frontend's explicit smoke setting.
        # A minimal PATH plus removed Qt overrides proves the ZIP is self-contained.
        $taskInfo.Environment['PATH'] = (Join-Path $env:SystemRoot 'System32') + ';' + $env:SystemRoot
        foreach ($taskVariable in @('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH', 'QTDIR', 'CMAKE_PREFIX_PATH')) {
            $taskInfo.Environment.Remove($taskVariable) | Out-Null
        }
        $taskInfo.Environment['QT_QPA_PLATFORM'] = $Platform
        $taskInfo.Environment['QT_QUICK_BACKEND'] = 'software'
        $taskInfo.Environment['QT_ASSUME_STDERR_HAS_CONSOLE'] = '1'
        $taskInfo.Environment['QT_FORCE_STDERR_LOGGING'] = '1'
        $taskInfo.Environment['APPDATA'] = Join-Path $taskWork 'Roaming'
        $taskInfo.Environment['LOCALAPPDATA'] = Join-Path $taskWork 'Local'
    }
    if ($PoisonQtEnvironment) {
        Assert-Condition ($Application -and (ConvertTo-NormalTestPath $Executable).Length -le 240) 'Environment poisoning is restricted to a short packaged desktop executable.'
        $taskForeignQt = Assert-Inside (Join-Path $taskWork 'ForeignQt') $taskWork
        $taskForeignBin = Join-Path $taskForeignQt 'bin'
        $taskForeignPlugins = Join-Path $taskForeignQt 'plugins'
        $taskForeignQml = Join-Path $taskForeignQt 'qml'
        foreach ($taskDirectory in @($taskForeignBin, $taskForeignPlugins, $taskForeignQml)) {
            [IO.Directory]::CreateDirectory($taskDirectory) | Out-Null
        }
        [IO.File]::WriteAllText((Join-Path $taskForeignBin 'Qt6Core.dll'), 'Invalid DLL sentinel for packaged-runtime isolation validation.')
        # Set these after the ordinary helper cleanup. Success must come from
        # production startup rejecting this external Qt environment itself.
        $taskInfo.Environment['QT_QPA_PLATFORM'] = 'ananarchiver-validation-missing-platform'
        $taskInfo.Environment['QT_PLUGIN_PATH'] = $taskForeignPlugins
        $taskInfo.Environment['QT_QPA_PLATFORM_PLUGIN_PATH'] = $taskForeignPlugins
        $taskInfo.Environment['QML_IMPORT_PATH'] = $taskForeignQml
        $taskInfo.Environment['QML2_IMPORT_PATH'] = $taskForeignQml
        $taskInfo.Environment['QTDIR'] = $taskForeignQt
        $taskInfo.Environment['PATH'] = $taskForeignBin + ';' + $taskInfo.Environment['PATH']
    }
    $taskUseNative = (ConvertTo-NormalTestPath $taskNativeExe).Length -gt 240
    $taskStdoutLog = Join-Path $taskOutput ($Name + '.stdout.log')
    $taskStderrLog = Join-Path $taskOutput ($Name + '.stderr.log')
    $taskProcess = $null
    $taskModules = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $taskTimer = [Diagnostics.Stopwatch]::StartNew()
    try {
        if ($taskUseNative) {
            if (-not ('AnAnArchiver.PackageValidation.NativeProcess' -as [type])) {
                Add-Type -Path (Join-Path $PSScriptRoot 'TestPackageNativeProcess.cs')
            }
            # Explicit lpApplicationName is required beyond MAX_PATH. Keep the
            # registered executable untouched when UseExactExecutable is set.
            $taskProcess = [AnAnArchiver.PackageValidation.NativeProcess]::Start($taskInfo,
                (ConvertTo-NativeTestPath $taskStdoutLog), (ConvertTo-NativeTestPath $taskStderrLog))
        } else {
            $taskProcess = [Diagnostics.Process]::new()
            $taskProcess.StartInfo = $taskInfo
            Assert-Condition ($taskProcess.Start()) ('Could not start ' + $Name)
            $taskStdout = $taskProcess.StandardOutput.ReadToEndAsync()
            $taskStderr = $taskProcess.StandardError.ReadToEndAsync()
        }
        while (-not $taskProcess.WaitForExit(100)) {
            if ($taskUseNative -and $Application) {
                # Qt can show a blocking native error box on platform-load failure.
                # Read the inherited log while it is open and terminate only this
                # test child promptly, retaining the complete diagnostic file.
                $taskLogStream = [IO.File]::Open($taskStderrLog, [IO.FileMode]::Open,
                    [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
                $taskLogReader = [IO.StreamReader]::new($taskLogStream)
                try { $taskPartialError = $taskLogReader.ReadToEnd() } finally { $taskLogReader.Dispose() }
                if ($taskPartialError.Contains('Could not load the Qt platform plugin') -or
                    $taskPartialError.Contains('no Qt platform plugin could be initialized')) {
                    $taskProcess.Kill($true)
                    throw ($Name + " failed to initialize its Qt platform:`n" + $taskPartialError)
                }
            }
            if ($ObserveQtModules) {
                try {
                    $taskProcess.Refresh()
                    foreach ($taskModule in $taskProcess.Modules) {
                        if ($taskModule.ModuleName -match '^Qt6.*\.dll$') { $taskModules.Add($taskModule.FileName) | Out-Null }
                    }
                } catch [ComponentModel.Win32Exception], [InvalidOperationException] {
                    # The child may exit between WaitForExit and module enumeration.
                }
            }
            if ($taskTimer.Elapsed.TotalSeconds -gt $TimeoutSeconds) {
                $taskProcess.Kill($true)
                throw ($Name + ' timed out after ' + $TimeoutSeconds + ' seconds.')
            }
        }
        if ($taskUseNative) {
            $taskOut = [IO.File]::ReadAllText($taskStdoutLog)
            $taskErr = [IO.File]::ReadAllText($taskStderrLog)
        } else {
            $taskOut = $taskStdout.GetAwaiter().GetResult()
            $taskErr = $taskStderr.GetAwaiter().GetResult()
            [IO.File]::WriteAllText($taskStdoutLog, $taskOut)
            [IO.File]::WriteAllText($taskStderrLog, $taskErr)
        }
        $taskRecord = [ordered]@{
            name = $Name; executable = $Executable; native_executable = $taskNativeExe; exact_executable = [bool] $UseExactExecutable
            native_create_process = $taskUseNative
            poisoned_qt_environment = [bool] $PoisonQtEnvironment
            arguments = $Arguments; exit_code = $taskProcess.ExitCode
            elapsed_seconds = [Math]::Round($taskTimer.Elapsed.TotalSeconds, 3); loaded_qt_modules = @($taskModules)
        }
        $taskProcesses.Add($taskRecord)
        if ($ExpectFailure) {
            Assert-Condition ($taskProcess.ExitCode -ne 0) ($Name + ' unexpectedly succeeded.')
        } else {
            Assert-Condition ($taskProcess.ExitCode -eq $ExpectedExitCode) ($Name + ' exited with ' + $taskProcess.ExitCode + ":`n" + $taskOut + $taskErr)
        }
        if ($ObserveQtModules) {
            Assert-Condition ($taskModules.Count -gt 0) ('No Qt DLL load evidence was collected for ' + $Name)
            $taskModuleRoot = [IO.Path]::GetDirectoryName((ConvertTo-NormalTestPath $Executable))
            foreach ($taskModule in $taskModules) { Assert-Inside $taskModule $taskModuleRoot | Out-Null }
        }
        return $taskRecord
    } finally { if ($taskProcess) { $taskProcess.Dispose() } }
}
function Get-RegistrySnapshot([string[]] $Paths) {
    $taskSnapshot = [ordered]@{}
    function Read-Key([Microsoft.Win32.RegistryKey] $Key, [string] $Path) {
        $taskValues = [ordered]@{}
        foreach ($taskName in @($Key.GetValueNames() | Sort-Object)) {
            $taskValue = $Key.GetValue($taskName, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            if ($taskValue -is [byte[]]) { $taskValue = [Convert]::ToHexString($taskValue) }
            $taskValues[$taskName] = @{ kind = $Key.GetValueKind($taskName).ToString(); value = $taskValue }
        }
        $taskSnapshot[$Path] = $taskValues
        foreach ($taskChild in @($Key.GetSubKeyNames() | Sort-Object)) {
            $taskChildKey = $Key.OpenSubKey($taskChild)
            try { Read-Key $taskChildKey ($Path + '\' + $taskChild) } finally { $taskChildKey.Dispose() }
        }
    }
    foreach ($taskPath in $Paths) {
        $taskKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($taskPath)
        if ($taskKey) { try { Read-Key $taskKey $taskPath } finally { $taskKey.Dispose() } }
        else { $taskSnapshot[$taskPath] = $null }
    }
    return ($taskSnapshot | ConvertTo-Json -Depth 25 -Compress)
}
function Get-TestRegistryValue([string] $Subkey, [string] $Name = '') {
    Assert-Condition ($taskRegistryPrefix -match '^Software\\AnAnArchiver\\PackageValidation\\[0-9a-f-]{36}$') 'Invalid registry test namespace.'
    $taskKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($taskRegistryPrefix + '\' + $Subkey)
    if (-not $taskKey) { return $null }
    try { return $taskKey.GetValue($Name) } finally { $taskKey.Dispose() }
}
function Test-TestRegistryValueExists([string] $Subkey, [string] $Name) {
    $taskKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($taskRegistryPrefix + '\' + $Subkey)
    if (-not $taskKey) { return $false }
    try { return $taskKey.GetValueNames() -contains $Name } finally { $taskKey.Dispose() }
}
function Get-TestUninstaller([switch] $AllowFallback) {
    $taskExpectedData = Assert-Inside (Join-Path $taskWork ('AnAnArchiver-InstallerData-' + $taskId)) $taskWork
    $taskUninstallCommand = Get-TestRegistryValue 'Uninstall\AnAnArchiver' 'UninstallString'
    $taskCandidate = $null
    if ($taskUninstallCommand) {
        Assert-Condition ($taskUninstallCommand -match '^"([^"]+)"$') 'The isolated uninstall command must contain one quoted executable and no arguments.'
        $taskCandidate = Assert-Inside $Matches[1] $taskWork
        Assert-Condition ([IO.Path]::GetDirectoryName($taskCandidate).Equals($taskExpectedData, [StringComparison]::OrdinalIgnoreCase)) 'The isolated uninstaller points outside its generated installer-data directory.'
        Assert-Condition ([IO.Path]::GetFileName($taskCandidate) -match '^unins[0-9]{3}\.exe$') 'The isolated uninstaller filename is invalid.'
        if (-not [IO.File]::Exists($taskCandidate)) {
            if (-not $AllowFallback) { throw 'The registered isolated uninstaller is missing.' }
            $taskCandidate = $null
        }
    } elseif (-not $AllowFallback) { throw 'The isolated uninstall registration is missing.' }
    if (-not $taskCandidate -and $AllowFallback -and [IO.Directory]::Exists($taskExpectedData)) {
        # Early setup failures may precede registration. Search only our exact GUID
        # data directory; never inspect or execute an uninstaller in a real profile.
        $taskFallbacks = @(Get-ChildItem -LiteralPath $taskExpectedData -File -Filter 'unins*.exe' |
            Where-Object Name -Match '^unins[0-9]{3}\.exe$' | Sort-Object LastWriteTime -Descending)
        if ($taskFallbacks.Count) { $taskCandidate = Assert-Inside $taskFallbacks[0].FullName $taskExpectedData }
    }
    if ($taskCandidate) {
        $taskRootRecord = Join-Path $taskExpectedData 'install-root.txt'
        if ([IO.File]::Exists($taskRootRecord)) {
            $taskRecordedRoots = [IO.File]::ReadAllLines($taskRootRecord)
            Assert-Condition ($taskRecordedRoots.Length -eq 1 -and $taskRecordedRoots[0].Equals($taskInstall, [StringComparison]::OrdinalIgnoreCase)) 'The generated uninstaller records a different installation root.'
            Assert-Inside $taskRecordedRoots[0] $taskWork | Out-Null
        } elseif (-not $AllowFallback) { throw 'The isolated uninstaller has no installation-root record.' }
    }
    return $taskCandidate
}
function Remove-TestWorkDirectory {
    Assert-Inside $taskWork $taskTempRoot | Out-Null
    for ($taskAttempt = 0; $taskAttempt -lt 30; $taskAttempt++) {
        if (-not [IO.Directory]::Exists($taskWork)) { return }
        try { Remove-Item -LiteralPath $taskWork -Recurse -Force; return }
        catch {
            if ($taskAttempt -eq 29) { throw }
            # Inno may briefly retain its final helper file after the parent exits.
            [Threading.Thread]::Sleep(100)
        }
    }
}
function Assert-Fixture([string] $Root, $Files) {
    foreach ($taskEntry in $Files.GetEnumerator()) {
        $taskFile = Join-Path $Root $taskEntry.Key
        Assert-Condition ([IO.File]::Exists($taskFile)) ('Missing extracted Unicode file: ' + $taskFile)
        Assert-Condition ([IO.File]::ReadAllText($taskFile) -ceq $taskEntry.Value) ('Extracted contents differ: ' + $taskFile)
    }
}
function Get-DirectorySnapshot([string] $Root) {
    $taskFiles = @(Get-ChildItem -LiteralPath $Root -Recurse -File | Sort-Object FullName | ForEach-Object {
        [ordered]@{ path = $_.FullName.Substring($Root.Length + 1); size = $_.Length;
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
    return ($taskFiles | ConvertTo-Json -Depth 5 -Compress)
}
function Invoke-Installer([string] $Executable, [string] $Name, [string] $Tasks,
                          [string] $InstallDirectory = $taskInstall, [switch] $ExpectFailure) {
    Invoke-TestProcess -Executable $Executable -Arguments @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-',
        ('/DIR=' + $InstallDirectory), ('/REGISTRYPREFIX=' + $taskRegistryPrefix), ('/TASKS=' + $Tasks),
        ('/LOG=' + (Join-Path $taskOutput ($Name + '.inno.log')))) -Name $Name -TimeoutSeconds 180 -ExpectFailure:$ExpectFailure | Out-Null
}

$taskExtensions = @('.zip', '.zipx', '.7z', '.rar', '.tar', '.gz', '.tgz', '.bz2', '.tbz', '.tbz2', '.xz', '.txz',
    '.zst', '.tzst', '.lz4', '.cab', '.cpio', '.ar', '.iso', '.lha', '.lzh', '.xar')
$taskRealKeys = @('Software\Classes\Applications\AnAnArchiver.exe', 'Software\Classes\AnAnArchiver.Archive',
    'Software\Classes\*\shell\AnAnArchiver.Compress', 'Software\Classes\Directory\shell\AnAnArchiver.Compress',
    'Software\RegisteredApplications', 'Software\AnAnArchiver\Capabilities', 'Software\AnAnArchiver\Integration',
    'Software\AnAnArchiver\AnAnArchiver', 'Software\Microsoft\Windows\CurrentVersion\Uninstall\AnAnArchiver',
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\AnAnArchiver_is1',
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\{B5695574-44A8-4DAB-AD67-31776F46CC31}_is1')
$taskRealKeys += $taskExtensions | ForEach-Object {
    'Software\Classes\' + $_
    'Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\' + $_ + '\UserChoice'
    foreach ($taskVerb in @('Open', 'ExtractHere', 'ExtractToFolder')) {
        'Software\Classes\SystemFileAssociations\' + $_ + '\shell\AnAnArchiver.' + $taskVerb
    }
}
$taskRegistryBefore = Get-RegistrySnapshot $taskRealKeys
try {
    # The generated directory is unique; never delete, move, or extract over an existing user's checkout.
    $taskExpanded = Join-Path $taskWork '便携 ZIP 解压 空格'
    [IO.Directory]::CreateDirectory($taskExpanded) | Out-Null
    [IO.Compression.ZipFile]::ExtractToDirectory($taskArchive, $taskExpanded)
    $taskExecutables = @(Get-ChildItem -LiteralPath $taskExpanded -Recurse -File -Filter AnAnArchiver.exe)
    Assert-Condition ($taskExecutables.Count -eq 1) 'The ZIP must contain exactly one desktop executable.'
    $taskExe = $taskExecutables[0].FullName
    $taskPackage = Split-Path $taskExe -Parent
    $taskManifest = Get-Content -LiteralPath (Join-Path $taskPackage 'package-manifest.json') -Raw | ConvertFrom-Json
    $taskVerifiedFiles = 0
    foreach ($taskFile in $taskManifest.files) {
        $taskDeployedFile = Assert-Inside (Join-Path $taskPackage $taskFile.relative_path) $taskPackage
        Assert-Condition ((Get-FileHash -LiteralPath $taskDeployedFile -Algorithm SHA256).Hash -ieq $taskFile.sha256) ('Payload hash differs: ' + $taskFile.relative_path)
        $taskVerifiedFiles++
    }
    Assert-Condition ($taskVerifiedFiles -gt 0) 'The package manifest contains no files.'
    Assert-Condition (Test-Path -LiteralPath (Join-Path $taskPackage 'qt.conf')) 'qt.conf is missing.'
    Assert-Condition (Test-Path -LiteralPath (Join-Path $taskPackage 'plugins/platforms/qwindows.dll')) 'The native Windows Qt platform plugin is missing.'
    Assert-Condition (Test-Path -LiteralPath (Join-Path $taskPackage 'qml/QtQuick/Controls/Basic/qmldir')) 'The Qt Quick Controls Basic module is missing.'
    Complete-Check 'portable payload SHA256 and Qt structure' @{ files = $taskVerifiedFiles; executable = $taskExe }

    if (-not $QtPrefix) {
        $taskCache = Get-Content -LiteralPath (Join-Path $taskBuild 'CMakeCache.txt')
        $taskQtLine = $taskCache | Where-Object { $_ -match '^Qt6Core_DIR:PATH=' } | Select-Object -First 1
        Assert-Condition ([bool] $taskQtLine) 'The build cache does not identify its Qt kit.'
        $QtPrefix = [IO.Path]::GetFullPath((Join-Path $taskQtLine.Substring('Qt6Core_DIR:PATH='.Length) '../../..'))
    }
    $taskQtPrefix = (Resolve-Path -LiteralPath $QtPrefix).Path
    $taskQtDllCount = 0
    $taskQtRecordedPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($taskDll in $taskManifest.deployment.qt_dlls) {
        $taskDllPath = Assert-Inside (Join-Path $taskPackage $taskDll.relative_path) $taskPackage
        $taskSource = Assert-Inside $taskDll.source_path $taskQtPrefix
        $taskSourceHash = (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash
        Assert-Condition ($taskSourceHash -ieq $taskDll.sha256) ('Deployment source hash differs: ' + $taskDll.source_path)
        Assert-Condition ((Get-FileHash -LiteralPath $taskDllPath -Algorithm SHA256).Hash -ieq $taskSourceHash) ('Qt kit/ABI mismatch: ' + $taskDll.relative_path)
        $taskQtRecordedPaths.Add($taskDll.relative_path.Replace('\', '/')) | Out-Null
        $taskQtDllCount++
    }
    $taskQtDlls = @(Get-ChildItem -LiteralPath $taskPackage -Recurse -File -Filter 'Qt6*.dll')
    Assert-Condition ($taskQtDllCount -ge $taskQtDlls.Count -and $taskQtDlls.Count -gt 0) 'Qt DLL provenance is incomplete.'
    foreach ($taskDll in $taskQtDlls) {
        $taskRelative = $taskDll.FullName.Substring($taskPackage.Length + 1).Replace('\', '/')
        Assert-Condition ($taskQtRecordedPaths.Contains($taskRelative)) ('Qt DLL provenance is missing: ' + $taskRelative)
    }
    Complete-Check 'deployed Qt DLLs match the build kit byte for byte' @{ dlls = $taskQtDllCount; qt_prefix = $taskQtPrefix }
    $taskCrtCount = 0
    foreach ($taskDll in $taskManifest.deployment.compiler_runtime_dlls) {
        $taskDeployed = Assert-Inside (Join-Path $taskPackage $taskDll.relative_path) $taskPackage
        $taskSourceHash = (Get-FileHash -LiteralPath $taskDll.source_path -Algorithm SHA256).Hash
        Assert-Condition ($taskSourceHash -ieq $taskDll.sha256 -and
            (Get-FileHash -LiteralPath $taskDeployed -Algorithm SHA256).Hash -ieq $taskSourceHash) ('Compiler runtime deployment differs: ' + $taskDll.relative_path)
        $taskCrtCount++
    }
    Assert-Condition ($taskCrtCount -ge 2) 'App-local MSVC runtime deployment is incomplete.'
    Complete-Check 'app-local compiler runtime matches the selected redistributable' @{ dlls = $taskCrtCount }

    foreach ($taskLanguage in @('en', 'zh-CN')) {
        Invoke-TestProcess $taskExe @('--smoke-test', '1800', '--language', $taskLanguage, '--smoke-screenshot',
            (Join-Path $taskOutput ('portable-home-' + $taskLanguage + '.png'))) ('portable-home-' + $taskLanguage) -Application -ObserveQtModules -Platform windows | Out-Null
    }
    Complete-Check 'portable native launch in English and Chinese with a minimal PATH' 'Qt modules loaded only from the extracted ZIP; screenshots saved.'
    Invoke-TestProcess $taskExe @('--smoke-test', '1800') 'portable-home-poisoned-qt' -Application -ObserveQtModules -PoisonQtEnvironment | Out-Null
    Complete-Check 'packaged startup ignores foreign Qt paths and an invalid inherited platform' 'The child received poisoned Qt/QML directories, PATH and platform; every loaded Qt DLL still came from the package.'

    $taskFixtureFiles = [ordered]@{ '中文目录/安安.txt' = '中文测试 Unicode payload'; '日本語/こんにちは.txt' = 'Japanese filename'; 'emoji/可可🐄🐈.txt' = 'emoji filename'; 'accents/café.txt' = 'accented filename' }
    $taskFixture = Join-Path $taskWork '测试 归档 安安.zip'
    $taskZip = [IO.Compression.ZipFile]::Open($taskFixture, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($taskEntry in $taskFixtureFiles.GetEnumerator()) {
            $taskEntryStream = $taskZip.CreateEntry($taskEntry.Key).Open()
            $taskWriter = [IO.StreamWriter]::new($taskEntryStream, [Text.UTF8Encoding]::new($false))
            try { $taskWriter.Write($taskEntry.Value) } finally { $taskWriter.Dispose() }
        }
    } finally { $taskZip.Dispose() }
    $taskInvalidCli = @(
        @{ name = 'invalid-shell-without-action'; args = @('--smoke-shell') },
        @{ name = 'invalid-compress-without-input'; args = @('--compress') },
        @{ name = 'invalid-extract-multiple-archives'; args = @('--extract-here', '--smoke-shell', $taskFixture, $taskFixture) },
        @{ name = 'invalid-mutually-exclusive-actions'; args = @('--extract-here', '--extract-to-folder', '--smoke-shell', $taskFixture) },
        @{ name = 'invalid-archive-smoke-with-shell-action'; args = @('--extract-here', '--smoke-archive', $taskFixture) }
    )
    foreach ($taskCase in $taskInvalidCli) {
        Invoke-TestProcess $taskExe $taskCase.args $taskCase.name -Application -ExpectedExitCode 2 | Out-Null
    }
    Complete-Check 'invalid shell command combinations fail before starting archive work' @{ cases = $taskInvalidCli.Count; exit_code = 2 }
    $taskLongDirectory = Join-Path $taskWork '长路径 中文 测试'
    while ($taskLongDirectory.Length -lt 290) { $taskLongDirectory = Join-Path $taskLongDirectory '包含空格的中文子目录 abcdefghijklmnopqrstuvwxyz' }
    [IO.Directory]::CreateDirectory($taskLongDirectory) | Out-Null
    $taskLongArchive = Join-Path $taskLongDirectory '内容检测 中文测试.dat'
    [IO.File]::Copy($taskFixture, $taskLongArchive)
    Invoke-TestProcess $taskExe @('--smoke-archive', '--language', 'zh-CN', $taskLongArchive) 'portable-long-unicode-archive' -Application | Out-Null
    Complete-Check 'Unicode long paths and content detection without an archive extension' @{ path_length = $taskLongArchive.Length; entries = $taskFixtureFiles.Count }

    $taskHere = Join-Path $taskWork '右键 就地解压 中文'
    [IO.Directory]::CreateDirectory($taskHere) | Out-Null
    $taskHereArchive = Join-Path $taskHere '归档 空格.zip'
    [IO.File]::Copy($taskFixture, $taskHereArchive)
    Invoke-TestProcess $taskExe @('--extract-here', '--smoke-shell', $taskHereArchive) 'portable-extract-here' -Application | Out-Null
    Assert-Fixture $taskHere $taskFixtureFiles
    $taskFolderArchive = Join-Path $taskLongDirectory '单独 文件夹.zip'
    [IO.File]::Copy($taskFixture, $taskFolderArchive)
    Invoke-TestProcess $taskExe @('--extract-to-folder', '--smoke-shell', $taskFolderArchive) 'portable-extract-to-folder' -Application | Out-Null
    Assert-Fixture (Join-Path $taskLongDirectory '单独 文件夹') $taskFixtureFiles
    Invoke-TestProcess $taskExe @('--compress', '--smoke-shell', (Join-Path $taskHere '中文目录/安安.txt'),
        (Join-Path $taskHere '日本語/こんにちは.txt')) 'portable-compress-selection' -Application | Out-Null
    Complete-Check 'shell actions extract Unicode payloads and prefill compression' 'Both extraction destinations checked; two compression inputs accepted.'

    $taskTests = & $taskCtest --test-dir $taskBuild --show-only=json-v1 | ConvertFrom-Json
    $taskTestNames = @($taskTests.tests.name)
    Assert-PackageRegressionCoverage $taskTestNames
    Invoke-TestProcess $taskCtest @('--test-dir', $taskBuild, '--output-on-failure', '--parallel', "$Jobs", '--output-junit',
        (Join-Path $taskOutput 'regression-results.xml')) 'core-qt-regressions' -TimeoutSeconds 600 | Out-Null
    Complete-Check 'Core, Application, and production Qt desktop regressions' @{ suites = $taskTestNames.Count; junit = 'regression-results.xml' }

    Invoke-Installer $taskInstaller 'install-without-integration' ''
    $taskInstalledExe = Join-Path $taskInstall 'AnAnArchiver.exe'
    Assert-Condition ([IO.File]::Exists($taskInstalledExe)) 'The installer did not deploy the executable.'
    foreach ($taskFile in $taskManifest.files) {
        # The older fixture lists additional obsolete files to exercise upgrade cleanup.
        if ($taskFile.relative_path -eq 'package-owned-files.txt') { continue }
        $taskInstalledFile = Assert-Inside (Join-Path $taskInstall $taskFile.relative_path) $taskInstall
        Assert-Condition ((Get-FileHash -LiteralPath $taskInstalledFile -Algorithm SHA256).Hash -ieq $taskFile.sha256) ('Installed payload hash differs: ' + $taskFile.relative_path)
    }
    Assert-Condition (-not (Get-TestRegistryValue 'Classes\AnAnArchiver.Archive')) 'Associations were registered when the optional task was disabled.'
    $taskInstalledVersion = Get-TestRegistryValue 'Uninstall\AnAnArchiver' 'DisplayVersion'
    Assert-Condition ([bool] $taskInstalledVersion) 'The isolated uninstall registration does not record its installed version.'
    Assert-Condition ((Get-TestRegistryValue 'Uninstall\AnAnArchiver' 'InstallLocation') -ceq $taskInstall) 'The installer did not retain the original long installation path in its metadata.'
    $taskUninstaller = Get-TestUninstaller
    $taskInstallerData = Split-Path $taskUninstaller -Parent
    Assert-Inside $taskInstallerData $taskWork | Out-Null
    Assert-Condition ($taskInstallerData.Length -lt 260) 'The isolated uninstaller data path exceeds its supported native path length.'
    Complete-Check 'uninstaller data is isolated and records the original long application root' @{ application_root_length = $taskInstall.Length; installer_data = $taskInstallerData }
    Invoke-TestProcess $taskInstalledExe @('--smoke-test', '1800') 'installed-launch' -Application -ObserveQtModules -Platform windows | Out-Null
    Complete-Check 'silent per-user install into a long Chinese path with optional integration disabled' @{ path = $taskInstall; path_length = $taskInstall.Length; version = $taskInstalledVersion }

    # Existing COM shell handlers can have only DelegateExecute and no default command.
    # They still own their verb; enabling integration must fail before replacing them.
    $taskForeignVerb = 'Classes\SystemFileAssociations\.zip\shell\AnAnArchiver.Open'
    $taskExistingVerb = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($taskRegistryPrefix + '\' + $taskForeignVerb)
    $taskVerbExists = [bool] $taskExistingVerb
    if ($taskExistingVerb) { $taskExistingVerb.Dispose() }
    Assert-Condition (-not $taskVerbExists) 'The disabled installer unexpectedly created the archive Open verb.'
    $taskDelegateExecute = '{00000000-0000-0000-0000-000000000001}'
    $taskForeignCommand = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($taskRegistryPrefix + '\' + $taskForeignVerb + '\command')
    try { $taskForeignCommand.SetValue('DelegateExecute', $taskDelegateExecute) } finally { $taskForeignCommand.Dispose() }
    $taskForeignFiles = Get-DirectorySnapshot $taskInstall
    $taskForeignData = Get-DirectorySnapshot $taskInstallerData
    $taskForeignRegistry = Get-RegistrySnapshot @($taskRegistryPrefix)
    Invoke-Installer $taskUpgradeInstaller 'reject-foreign-com-shell-verb' 'associations,contextmenu' -ExpectFailure
    Assert-Condition ((Get-DirectorySnapshot $taskInstall) -ceq $taskForeignFiles) 'A conflicting COM shell verb changed the installed files.'
    Assert-Condition ((Get-DirectorySnapshot $taskInstallerData) -ceq $taskForeignData) 'A conflicting COM shell verb changed the uninstaller data.'
    Assert-Condition ((Get-RegistrySnapshot @($taskRegistryPrefix)) -ceq $taskForeignRegistry) 'A conflicting COM shell verb was changed or other registration was written.'
    Assert-Condition ((Get-TestRegistryValue ($taskForeignVerb + '\command') 'DelegateExecute') -ceq $taskDelegateExecute) 'The installer changed a foreign DelegateExecute handler.'
    Assert-Condition (-not (Test-TestRegistryValueExists ($taskForeignVerb + '\command') '')) 'The installer added a command to a foreign COM handler.'
    [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($taskRegistryPrefix + '\' + $taskForeignVerb, $false)
    Complete-Check 'foreign COM shell verbs are preserved and block conflicting integration' 'DelegateExecute-only handler and complete installation snapshots remained unchanged; only the generated test verb was removed.'

    $taskOriginalFiles = Get-DirectorySnapshot $taskInstall
    $taskOriginalData = Get-DirectorySnapshot $taskInstallerData
    $taskOriginalRegistry = Get-RegistrySnapshot @($taskRegistryPrefix)
    $taskMovedInstall = Assert-Inside (Join-Path $taskWork '错误 迁移 目标\AnAnArchiver') $taskWork
    Invoke-Installer $taskUpgradeInstaller 'reject-installation-directory-move' 'associations,contextmenu' $taskMovedInstall -ExpectFailure
    Assert-Condition ((Get-DirectorySnapshot $taskInstall) -ceq $taskOriginalFiles) 'A rejected installation move changed the existing installation.'
    Assert-Condition ((Get-DirectorySnapshot $taskInstallerData) -ceq $taskOriginalData) 'A rejected installation move changed the uninstaller data.'
    Assert-Condition ((Get-RegistrySnapshot @($taskRegistryPrefix)) -ceq $taskOriginalRegistry) 'A rejected installation move changed the installation registration.'
    Assert-Condition (-not [IO.File]::Exists((Join-Path $taskMovedInstall 'AnAnArchiver.exe'))) 'A rejected installation move wrote the executable to the new target.'
    $taskMovedFiles = @(Get-ChildItem -LiteralPath $taskMovedInstall -Recurse -File -ErrorAction SilentlyContinue)
    Assert-Condition ($taskMovedFiles.Count -eq 0) 'A rejected installation move wrote managed files to the new target.'
    Complete-Check 'installation directory changes fail before writing files or registration' 'Existing installation and complete registration snapshots remain identical.'

    [IO.File]::WriteAllText((Join-Path $taskInstall 'user-note.txt'), 'Keep this user-owned file during upgrade and uninstall.')
    $taskObsoleteFile = Join-Path $taskInstall 'obsolete-component.txt'
    Assert-Condition ([IO.File]::Exists($taskObsoleteFile)) 'The older installer fixture must include obsolete-component.txt to exercise upgrade cleanup.'
    $taskModifiedOwnedFile = Join-Path $taskInstall 'user-modified-managed.txt'
    $taskHasModifiedOwnedFixture = [IO.File]::Exists($taskModifiedOwnedFile)
    $taskModifiedOwnedPayload = 'The user replaced this obsolete managed file. Preserve it during upgrade.'
    if ($taskHasModifiedOwnedFixture) { [IO.File]::WriteAllText($taskModifiedOwnedFile, $taskModifiedOwnedPayload) }
    $taskExistingZip = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($taskRegistryPrefix + '\Classes\.zip')
    try { $taskExistingZip.SetValue('', 'Other.Archiver') } finally { $taskExistingZip.Dispose() }
    Invoke-Installer $taskUpgradeInstaller 'upgrade-with-integration' 'associations,contextmenu'
    $taskUpgradedVersion = Get-TestRegistryValue 'Uninstall\AnAnArchiver' 'DisplayVersion'
    Assert-Condition ([version] $taskUpgradedVersion -gt [version] $taskInstalledVersion) 'Upgrade verification requires an older installer fixture followed by the current installer. Pass -InstallerPath <older fixture> and -UpgradeInstallerPath <current installer>.'
    Assert-Condition ((Get-TestRegistryValue 'Classes\.zip') -ceq 'Other.Archiver') 'Upgrade overwrote an existing default archive association.'
    Assert-Condition ([IO.File]::Exists((Join-Path $taskInstall 'user-note.txt'))) 'Upgrade removed a user-owned file.'
    Assert-Condition (-not [IO.File]::Exists($taskObsoleteFile)) 'Upgrade retained an unmodified obsolete managed file.'
    if ($taskHasModifiedOwnedFixture) {
        Assert-Condition ([IO.File]::Exists($taskModifiedOwnedFile) -and
            [IO.File]::ReadAllText($taskModifiedOwnedFile) -ceq $taskModifiedOwnedPayload) 'Upgrade removed or replaced a user-modified obsolete managed file.'
    }
    foreach ($taskFile in $taskManifest.files) {
        $taskUpgradedFile = Assert-Inside (Join-Path $taskInstall $taskFile.relative_path) $taskInstall
        Assert-Condition ((Get-FileHash -LiteralPath $taskUpgradedFile -Algorithm SHA256).Hash -ieq $taskFile.sha256) ('Upgraded payload hash differs: ' + $taskFile.relative_path)
    }
    Complete-Check 'upgrade cleans obsolete managed files and preserves user changes' @{ obsolete_file_removed = $true; user_modified_managed_fixture = $taskHasModifiedOwnedFixture }
    # The installer uses the same integration implementation in its strictly restricted test namespace.
    $taskRegistryEnabled = Get-RegistrySnapshot @(($taskRegistryPrefix + '\Classes'), ($taskRegistryPrefix + '\RegisteredApplications'), ($taskRegistryPrefix + '\Uninstall'))
    [IO.File]::WriteAllText((Join-Path $taskOutput 'integration-enabled.json'), $taskRegistryEnabled)
    Assert-Condition ($taskRegistryEnabled.Contains('--extract-here') -and $taskRegistryEnabled.Contains('--extract-to-folder') -and
        $taskRegistryEnabled.Contains('--compress') -and $taskRegistryEnabled.Contains('OpenWithProgids')) 'Optional shell commands or Open With registration are missing.'
    Assert-Condition ($taskRegistryEnabled.Contains('AnAnArchiver.exe') -and $taskRegistryEnabled.Contains('%1')) 'Shell commands do not target the installed executable and selected path.'
    $taskQuotedExe = '"' + (ConvertTo-NativeTestPath $taskInstalledExe 248) + '"'
    Assert-Condition ((Get-TestRegistryValue 'Classes\AnAnArchiver.Archive\shell\open\command') -ceq ($taskQuotedExe + ' "%1"')) 'The archive ProgID open command is incorrect.'
    foreach ($taskExtension in $taskExtensions) {
        Assert-Condition (Test-TestRegistryValueExists ('Classes\' + $taskExtension + '\OpenWithProgids') 'AnAnArchiver.Archive') ('Open With registration missing: ' + $taskExtension)
        foreach ($taskVerb in @(@('Open', ''), @('ExtractHere', '--extract-here '), @('ExtractToFolder', '--extract-to-folder '))) {
            $taskCommandPath = 'Classes\SystemFileAssociations\' + $taskExtension + '\shell\AnAnArchiver.' + $taskVerb[0] + '\command'
            $taskExpectedCommand = $taskQuotedExe + ' ' + $taskVerb[1] + '"%1"'
            Assert-Condition ((Get-TestRegistryValue $taskCommandPath) -ceq $taskExpectedCommand) ('Explorer archive command is incorrect: ' + $taskCommandPath)
        }
    }
    foreach ($taskType in @('*', 'Directory')) {
        Assert-Condition ((Get-TestRegistryValue ('Classes\' + $taskType + '\shell\AnAnArchiver.Compress\command')) -ceq
            ($taskQuotedExe + ' --compress "%1"')) ('Explorer compression command is incorrect for ' + $taskType)
    }
    $taskRegisteredCommand = Get-TestRegistryValue 'Classes\AnAnArchiver.Archive\shell\open\command'
    $taskRegisteredMatch = [regex]::Match($taskRegisteredCommand, '^"([^"]+)" "%1"$')
    Assert-Condition ($taskRegisteredMatch.Success) 'The registered archive Open command cannot be parsed.'
    $taskRegisteredExecutable = $taskRegisteredMatch.Groups[1].Value
    Assert-Inside $taskRegisteredExecutable $taskInstall | Out-Null
    # Keep the registered executable byte for byte. The helper must not quietly
    # repair an invalid long command that Windows receives from the registration.
    Invoke-TestProcess $taskRegisteredExecutable @('--smoke-archive', $taskFixture) 'upgraded-registered-archive-launch' -Application -ObserveQtModules -UseExactExecutable | Out-Null
    Complete-Check 'registered long-path archive command launches without path repair' @{ registered_executable = $taskRegisteredExecutable; command = $taskRegisteredCommand }
    Complete-Check 'in-place version upgrade, opt-in associations and Explorer commands' @{ from_version = $taskInstalledVersion; to_version = $taskUpgradedVersion; existing_default_preserved = $true; user_file_preserved = $true }

    Invoke-Installer $taskUpgradeInstaller 'upgrade-disable-integration' ''
    $taskRegistryDisabled = Get-RegistrySnapshot @(($taskRegistryPrefix + '\Classes'), ($taskRegistryPrefix + '\RegisteredApplications'))
    Assert-Condition (-not ($taskRegistryDisabled.Contains('--extract-here') -or $taskRegistryDisabled.Contains('--compress') -or
        $taskRegistryDisabled.Contains('AnAnArchiver.Archive'))) 'Disabling optional integration during upgrade retained AnAnArchiver registrations.'
    Assert-Condition ((Get-TestRegistryValue 'Classes\.zip') -ceq 'Other.Archiver') 'Disabling optional integration removed the existing default association.'
    Complete-Check 'upgrade can disable previously enabled optional integration' 'Application registration removed; the existing archive default retained.'

    $taskUninstaller = Get-TestUninstaller
    Invoke-TestProcess $taskUninstaller @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
        ('/LOG=' + (Join-Path $taskOutput 'uninstall.inno.log'))) 'uninstall' -TimeoutSeconds 180 | Out-Null
    for ($taskAttempt = 0; $taskAttempt -lt 50 -and [IO.Directory]::Exists($taskInstallerData); $taskAttempt++) {
        [Threading.Thread]::Sleep(100)
    }
    Assert-Condition (-not [IO.Directory]::Exists($taskInstallerData)) 'Uninstall retained its managed installer-data directory.'
    Assert-Condition (-not [IO.File]::Exists($taskInstalledExe)) 'Uninstall retained the managed application executable.'
    foreach ($taskFile in $taskManifest.files) {
        Assert-Condition (-not [IO.File]::Exists((Join-Path $taskInstall $taskFile.relative_path))) ('Uninstall retained a managed payload file: ' + $taskFile.relative_path)
    }
    $taskRemainingDlls = @(Get-ChildItem -LiteralPath $taskInstall -Recurse -File -Filter '*.dll' -ErrorAction SilentlyContinue)
    Assert-Condition ($taskRemainingDlls.Count -eq 0) 'Uninstall retained managed runtime DLLs.'
    Assert-Condition ([IO.File]::Exists((Join-Path $taskInstall 'user-note.txt'))) 'Uninstall removed a user-owned file.'
    Assert-Condition ((Get-TestRegistryValue 'Classes\.zip') -ceq 'Other.Archiver') 'Uninstall removed an unrelated default association.'
    $taskRegistryRemoved = Get-RegistrySnapshot @(($taskRegistryPrefix + '\Classes'), ($taskRegistryPrefix + '\RegisteredApplications'), ($taskRegistryPrefix + '\Uninstall'))
    Assert-Condition (-not $taskRegistryRemoved.Contains('AnAnArchiver.exe')) 'Uninstall retained application shell or uninstall registration.'
    Complete-Check 'uninstall removes managed payload and registration while preserving user files' 'User-owned note and unrelated .zip default retained.'
    $taskReport.status = 'passed'
} catch {
    $taskReport.status = 'failed'
    $taskReport.error = $_.Exception.Message
    throw
} finally {
    # The only writable registry root is validated again immediately before cleanup.
    try {
        $taskCleanupUninstaller = Get-TestUninstaller -AllowFallback
        if ($taskCleanupUninstaller) {
            Invoke-TestProcess $taskCleanupUninstaller @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
                ('/REGISTRYPREFIX=' + $taskRegistryPrefix)) 'cleanup-uninstall' -TimeoutSeconds 180 | Out-Null
        }
    } catch { $taskReport.cleanup_error = $_.Exception.Message; $taskReport.status = 'failed' }
    if ($taskRegistryPrefix -match '^Software\\AnAnArchiver\\PackageValidation\\[0-9a-f-]{36}$') {
        [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($taskRegistryPrefix, $false)
    }
    $taskRegistryAfter = Get-RegistrySnapshot $taskRealKeys
    if ($taskRegistryBefore -cne $taskRegistryAfter) {
        $taskReport.status = 'failed'
        $taskReport.registry_error = 'Real user associations or UserChoice changed during validation.'
    } else { Complete-Check 'real user associations and UserChoice remain unchanged' 'Compared read-only snapshots before and after validation.' }
    $taskReport.finished_utc = [DateTime]::UtcNow.ToString('o')
    if (-not $KeepWorkDirectory -and $taskReport.status -eq 'passed') {
        try { Remove-TestWorkDirectory }
        catch { $taskReport.cleanup_error = $_.Exception.Message; $taskReport.status = 'failed' }
    }
    [IO.File]::WriteAllText((Join-Path $taskOutput 'verification.json'), ($taskReport | ConvertTo-Json -Depth 30))
}
Assert-Condition ($taskReport.status -eq 'passed') ('Package verification failed. See ' + (Join-Path $taskOutput 'verification.json'))
Write-Host ('Package validation report: ' + (Join-Path $taskOutput 'verification.json'))
