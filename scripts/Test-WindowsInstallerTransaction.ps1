<#
.SYNOPSIS
Checks obsolete-file upgrade rollback with actual Inno installers and small payloads.
.DESCRIPTION
Every installation uses a private PackageValidation registry namespace and TEMP.
No release artifacts, real shortcuts, default applications or profile data are changed.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $InstallerCompiler,
    [string] $OutputDirectory = 'out/fix-main/installer-transaction',
    [string] $TemporaryRoot = $env:TEMP
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT') { throw 'This regression requires Windows.' }
$taskRepository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskOutput = if ([IO.Path]::IsPathRooted($OutputDirectory)) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $taskRepository $OutputDirectory }
$taskCompiler = (Resolve-Path -LiteralPath $InstallerCompiler).Path
$taskVersion = (& $taskCompiler --version | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $taskVersion -notmatch '\d+\.\d+\.\d+' -or [version]$Matches[0] -lt [version]'7.1.0') {
    throw 'Use Inno Setup 7.1 or newer for this regression.'
}
[IO.Directory]::CreateDirectory($taskOutput) | Out-Null
$taskRunId = [guid]::NewGuid().ToString('D')
$taskWork = Join-Path ([IO.Path]::GetFullPath($TemporaryRoot)) ('AAA-InstallerTxn-' + $taskRunId)
[IO.Directory]::CreateDirectory($taskWork) | Out-Null
$taskResults = [Collections.Generic.List[object]]::new()
function Assert-Task([bool] $Condition, [string] $Message) { if (-not $Condition) { throw $Message } }
function Assert-TaskInside([string] $Path, [string] $Root) {
    $normal = [IO.Path]::GetFullPath($Path)
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    Assert-Task ($normal.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) ('Test path escaped its private directory: ' + $Path)
    return $normal
}
function Invoke-TaskInstaller([string] $Exe, [string[]] $Arguments, [string] $Work) {
    $info = [Diagnostics.ProcessStartInfo]::new($Exe)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.Environment['TEMP'] = $Work
    $info.Environment['TMP'] = $Work
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($info)
    try {
        if (-not $process.WaitForExit(60000)) { $process.Kill($true); throw 'The private installer timed out.' }
        return $process.ExitCode
    } finally { $process.Dispose() }
}
function Get-TaskFiles([string] $Root) {
    return @(Get-ChildItem -LiteralPath $Root -File -Recurse | Sort-Object FullName | ForEach-Object {
        $_.FullName.Substring($Root.Length) + '|' + (Get-FileHash -LiteralPath $_.FullName).Hash
    }) -join "`n"
}
function Get-TaskRegistry([string] $Prefix) {
    $values = [Collections.Generic.List[string]]::new()
    function Visit-TaskKey([Microsoft.Win32.RegistryKey] $Key, [string] $Relative) {
        foreach ($name in $Key.GetValueNames() | Sort-Object) { $values.Add($Relative + '|' + $name + '|' + $Key.GetValueKind($name) + '|' + ($Key.GetValue($name) | ConvertTo-Json -Compress)) }
        foreach ($name in $Key.GetSubKeyNames() | Sort-Object) {
            $child = $Key.OpenSubKey($name)
            try { Visit-TaskKey $child ($Relative + '\' + $name) } finally { $child.Dispose() }
        }
    }
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($Prefix)
    if ($key) { try { Visit-TaskKey $key '' } finally { $key.Dispose() } }
    return $values -join "`n"
}
function New-TaskPayload([string] $Name, [bool] $Old) {
    $directory = Join-Path $taskOutput $Name
    Assert-Task (-not (Test-Path -LiteralPath $directory)) 'Use a fresh output directory for each regression run.'
    [IO.Directory]::CreateDirectory($directory) | Out-Null
    [IO.File]::WriteAllText((Join-Path $directory 'AnAnArchiver.exe'), $(if ($Old) { 'old-not-executed-fixture' } else { 'new-not-executed-fixture' }))
    if ($Old) {
        foreach ($name in @('001-obsolete.dat', '002-locked-obsolete.dat', 'modified-managed.dat', 'link/owned.dat')) {
            $file = Join-Path $directory $name
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file)) | Out-Null
            [IO.File]::WriteAllText($file, 'original-owned-content')
        }
    }
    $owned = @(Get-ChildItem -LiteralPath $directory -File -Recurse | Sort-Object FullName | ForEach-Object {
        $_.FullName.Substring($directory.Length + 1).Replace('\', '/') + '|' + (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()
    })
    [IO.File]::WriteAllLines((Join-Path $directory 'package-owned-files.txt'), $owned, [Text.UTF8Encoding]::new($false))
    return $directory
}
$taskOldPayload = New-TaskPayload ($taskRunId + '-old') $true
$taskNewPayload = New-TaskPayload ($taskRunId + '-new') $false
$taskIss = [IO.File]::ReadAllText((Join-Path $taskRepository 'packaging/windows/AnAnArchiver.iss'))
$taskInstallers = @{}
foreach ($name in @('old', 'new', 'payload-failure', 'restore-conflict')) {
    $source = $taskIss
    if ($name -in @('payload-failure', 'restore-conflict')) {
        # A missing external source exercises a real installation-engine error
        # after cleanup. BeforeInstall exceptions merely display a message and
        # can continue, so they are not a reliable cancellation fixture.
        $missing = Join-Path $taskOutput ($taskRunId + '-missing-source.dat')
        $callback = if ($name -eq 'restore-conflict') { '; BeforeInstall: RegressionCreateConflict' } else { '' }
        $entry = 'Source: "' + $missing + '"; DestDir: "{code:InstallRoot}"; DestName: "fault-trigger.dat"; Flags: external' + $callback
        $source = $source.Replace('[Files]', "[Files]`r`n" + $entry)
        if ($name -eq 'restore-conflict') {
            $source += "`r`nprocedure RegressionCreateConflict;`r`nbegin`r`n  SaveStringToFile(OwnedFileName('001-obsolete.dat'), 'unknown-new-content', False);`r`nend;`r`n"
        }
    }
    $iss = Join-Path $taskOutput ($taskRunId + '-' + $name + '.iss')
    [IO.File]::WriteAllText($iss, $source, [Text.UTF8Encoding]::new($false))
    $payload = if ($name -eq 'old') { $taskOldPayload } else { $taskNewPayload }
    $version = if ($name -eq 'old') { '0.9.0' } else { '1.0.0' }
    $outputName = $taskRunId + '-' + $name
    & $taskCompiler '/Qp' ('/DPackageRoot=' + $payload) ('/DOutputRoot=' + $taskOutput) ('/DAppVersion=' + $version) ('/DOutputName=' + $outputName) $iss
    if ($LASTEXITCODE -ne 0) { throw ('Could not compile fixture: ' + $name) }
    $taskInstallers[$name] = Join-Path $taskOutput ($outputName + '.exe')
}
try {
    foreach ($name in @('backup-failure', 'cleanup-failure', 'payload-failure', 'restore-conflict', 'successful-upgrade')) {
        $id = [guid]::NewGuid().ToString('D')
        $work = Join-Path $taskWork $name
        [IO.Directory]::CreateDirectory($work) | Out-Null
        $install = Join-Path $work '中文 空格 🐄🐈'
        while ($install.Length -lt 285) { $install = Join-Path $install '长路径 abcdefghijklmnop' }
        $prefix = 'Software\AnAnArchiver\PackageValidation\' + $id
        $data = Join-Path $work ('AnAnArchiver-InstallerData-' + $id)
        $base = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', ('/DIR=' + $install), ('/REGISTRYPREFIX=' + $prefix), '/TASKS=')
        $lock = $null
        $junction = $null
        try {
            $exit = Invoke-TaskInstaller $taskInstallers.old ($base + @('/LOG=' + (Join-Path $taskOutput ($taskRunId + '-' + $name + '-initial.log')))) $work
            Assert-Task ($exit -eq 0) ('Initial installation failed: ' + $name)
            $filesBefore = Get-TaskFiles $install
            $registryBefore = Get-TaskRegistry $prefix
            if ($name -eq 'backup-failure') {
                $lock = [IO.File]::Open((Join-Path $install '002-locked-obsolete.dat'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
            }
            if ($name -eq 'cleanup-failure') {
                $lock = [IO.File]::Open((Join-Path $install '002-locked-obsolete.dat'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
            }
            if ($name -eq 'successful-upgrade') {
                [IO.File]::WriteAllText((Join-Path $install 'modified-managed.dat'), 'user-modified-content')
                $junction = Join-Path $install 'link'
                $outside = Join-Path $work 'outside'
                [IO.Directory]::Move($junction, $outside)
                New-Item -ItemType Junction -Path $junction -Target $outside | Out-Null
            }
            $installer = if ($name -in @('payload-failure', 'restore-conflict')) { $taskInstallers[$name] } else { $taskInstallers.new }
            $log = Join-Path $taskOutput ($taskRunId + '-' + $name + '.log')
            $exit = Invoke-TaskInstaller $installer ($base + @('/LOG=' + $log)) $work
            if ($name -eq 'backup-failure') { $lock.Dispose(); $lock = $null }
            if ($name -eq 'successful-upgrade') {
                Assert-Task ($exit -eq 0) 'The normal upgrade failed.'
                foreach ($file in @('001-obsolete.dat', '002-locked-obsolete.dat')) { Assert-Task (-not [IO.File]::Exists((Join-Path $install $file))) ('Obsolete file remains: ' + $file) }
                Assert-Task ([IO.File]::ReadAllText((Join-Path $install 'modified-managed.dat')) -eq 'user-modified-content') 'Upgrade changed the modified file.'
                Assert-Task ([IO.File]::ReadAllText((Join-Path $work 'outside/owned.dat')) -eq 'original-owned-content') 'Upgrade followed a reparse path.'
                Assert-Task (@(Get-ChildItem -LiteralPath $data -Directory -Filter 'obsolete-rollback-*').Count -eq 0) 'Successful upgrade retained recovery data.'
            } else {
                Assert-Task ($exit -ne 0) ('The failing upgrade returned success: ' + $name)
                Assert-Task ((Get-TaskRegistry $prefix) -ceq $registryBefore) ('Failed upgrade changed integration registration: ' + $name)
                if ($name -eq 'restore-conflict') {
                    Assert-Task ([IO.File]::ReadAllText((Join-Path $install '001-obsolete.dat')) -eq 'unknown-new-content') 'Recovery overwrote an unknown new file.'
                    $backup = @(Get-ChildItem -LiteralPath $data -Directory -Filter 'obsolete-rollback-*')
                    Assert-Task ($backup.Count -eq 1) 'Recovery failure lost its persistent backup.'
                    Assert-Task ([IO.File]::ReadAllText((Join-Path $backup[0].FullName '0.bin')) -eq 'original-owned-content') 'Recovery backup is corrupt.'
                    Assert-Task ([IO.File]::Exists((Join-Path $backup[0].FullName 'restore-files.txt'))) 'Recovery instructions were not retained.'
                    Assert-Task ([IO.File]::ReadAllText($log).Contains('Upgrade recovery was incomplete')) 'Recovery failure was not reported.'
                } else {
                    Assert-Task ((Get-TaskFiles $install) -ceq $filesBefore) ('Failed upgrade changed the original payload: ' + $name)
                    Assert-Task (@(Get-ChildItem -LiteralPath $data -Directory -Filter 'obsolete-rollback-*').Count -eq 0) 'Successful recovery retained backup data.'
                    if ($name -eq 'backup-failure') {
                        Assert-Task (-not [IO.File]::ReadAllText($log).Contains('Removed unchanged obsolete package file')) 'Cleanup began before the complete backup set was ready.'
                    }
                }
            }
            $taskResults.Add([ordered]@{ case = $name; passed = $true; exit_code = $exit; application_path_length = $install.Length; log = $log })
        } finally {
            if ($lock) { $lock.Dispose() }
            if ($junction -and (Test-Path -LiteralPath $junction)) {
                Assert-TaskInside $junction $work | Out-Null
                Remove-Item -LiteralPath $junction -Force
                [IO.Directory]::Move((Join-Path $work 'outside'), $junction)
            }
            $uninstaller = Join-Path $data 'unins000.exe'
            if ([IO.File]::Exists($uninstaller)) {
                Assert-TaskInside $uninstaller $work | Out-Null
                $exit = Invoke-TaskInstaller $uninstaller @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', ('/REGISTRYPREFIX=' + $prefix)) $work
                Assert-Task ($exit -eq 0) ('Private test uninstall failed: ' + $name)
            }
            Assert-Task ($prefix -match '^Software\\AnAnArchiver\\PackageValidation\\[0-9a-f-]{36}$') 'Invalid cleanup namespace.'
            [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($prefix, $false)
        }
    }
} finally {
    [ordered]@{ installer_compiler = $taskCompiler; installer_version = $taskVersion; work_directory = $taskWork; checks = $taskResults.ToArray() } |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskOutput 'transaction-results.json') -Encoding utf8
}
Write-Host ('Installer transaction checks passed: ' + $taskResults.Count)
