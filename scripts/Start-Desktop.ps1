[CmdletBinding()]
param(
    [Parameter(Position = 0)] [string] $BuildDirectory = 'out/build/desktop-release',
    [Parameter(ValueFromRemainingArguments = $true)] [string[]] $ApplicationArguments
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskBuildPath = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
} else {
    Join-Path $repositoryRoot $BuildDirectory
}
$taskBuildPath = (Resolve-Path -LiteralPath $taskBuildPath).Path
$applicationDirectory = $taskBuildPath
$executable = Join-Path $applicationDirectory 'LiteArc.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    $applicationDirectory = Join-Path $taskBuildPath 'application'
    $executable = Join-Path $applicationDirectory 'LiteArc.exe'
}
if (-not (Test-Path -LiteralPath $executable)) {
    throw 'Desktop executable is missing. Build the Qt frontend first.'
}
$cache = Join-Path $applicationDirectory 'CMakeCache.txt'
$qtPackage = Get-Content -LiteralPath $cache | Where-Object { $_ -match '^Qt6Core_DIR:PATH=' } | Select-Object -First 1
if (-not $qtPackage) { throw 'The selected Qt kit could not be found in the application build cache.' }
$qtCoreDirectory = $qtPackage.Substring('Qt6Core_DIR:PATH='.Length)
$qtPrefix = [System.IO.Path]::GetFullPath((Join-Path $qtCoreDirectory '../../..'))
$qtRuntime = Join-Path $qtPrefix 'bin'
if (-not ((Test-Path -LiteralPath (Join-Path $qtRuntime 'Qt6Core.dll')) -or
          (Test-Path -LiteralPath (Join-Path $qtRuntime 'Qt6Cored.dll')))) {
    throw 'The selected Qt runtime is unavailable.'
}
# Use exactly the kit selected by CMake, including compiler ABI. Change only
# this process's environment; no deployment or global PATH modification.
$originalTaskPath = $env:PATH
try {
    $env:PATH = $qtRuntime + [System.IO.Path]::PathSeparator + $env:PATH
    # A pipeline waits for a Windows GUI executable too, so smoke failures
    # propagate and the selected runtime remains in scope until process exit.
    & $executable @ApplicationArguments | Out-Host
    $applicationExitCode = $LASTEXITCODE
} finally {
    $env:PATH = $originalTaskPath
}
exit $applicationExitCode
