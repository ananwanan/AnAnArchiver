[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $QtKit,
    [Parameter(Mandatory = $true)] [string] $CachedBuild,
    [string] $OutputKit,
    [string] $WorkDirectory,
    [int] $Jobs = 4
)
$ErrorActionPreference = 'Stop'
$taskRepository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskArguments = @((Join-Path $PSScriptRoot 'prepare-patched-qt-kit.py'),
    '--kit', $QtKit, '--cache', $CachedBuild, '--repository', $taskRepository, '--jobs', "$Jobs")
if ($OutputKit) { $taskArguments += @('--output', $OutputKit) }
if ($WorkDirectory) { $taskArguments += @('--work', $WorkDirectory) }
# Preserve the selected cache's MSVC flags/PCH while initializing its SDK/linker
# environment. The helper exports patched input and uses a private Ninja graph.
& (Join-Path $PSScriptRoot 'Invoke-Msvc.ps1') python @taskArguments
if ($LASTEXITCODE -ne 0) { throw 'Preparing the patched Qt kit failed.' }
