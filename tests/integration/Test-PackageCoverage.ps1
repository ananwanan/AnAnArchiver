[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $BuildDirectory,
    [string] $OutputDirectory = 'out/fix-main/package-coverage'
)
$ErrorActionPreference = 'Stop'
$taskRepository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $taskRepository 'scripts/Package-RegressionCoverage.ps1')
$taskBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path
if (Test-Path -LiteralPath (Join-Path $taskBuild 'application/CMakeCache.txt')) { $taskBuild = Join-Path $taskBuild 'application' }
$taskCacheHome = Get-Content -LiteralPath (Join-Path $taskBuild 'CMakeCache.txt') |
    Where-Object { $_ -match '^CMAKE_HOME_DIRECTORY:INTERNAL=' } | Select-Object -First 1
if (-not $taskCacheHome -or -not [IO.Path]::GetFullPath($taskCacheHome.Substring('CMAKE_HOME_DIRECTORY:INTERNAL='.Length)).Equals(
        $taskRepository, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Coverage regression requires a configured build of this source checkout.'
}
$taskListing = & ctest --test-dir $taskBuild --show-only=json-v1
if ($LASTEXITCODE -ne 0) { throw 'CTest could not list the configured desktop tests.' }
$taskTests = ($taskListing -join "`n") | ConvertFrom-Json
$taskNames = @($taskTests.tests.name)
Assert-PackageRegressionCoverage $taskNames
Write-Host 'PASS package coverage accepts the real configured desktop test list'
$taskMissingNames = @($taskNames | Where-Object { $_ -ne 'dependency_tools' })
$taskRejected = $false
try { Assert-PackageRegressionCoverage $taskMissingNames }
catch {
    if ($_.Exception.Message -cne 'Regression coverage is missing: dependency_tools') { throw }
    $taskRejected = $true
}
if (-not $taskRejected) { throw 'The package guard accepted a desktop build without its dependency tools regression.' }
Write-Host 'PASS removing dependency_tools is still rejected by the package guard'
$taskOutput = if ([IO.Path]::IsPathRooted($OutputDirectory)) { [IO.Path]::GetFullPath($OutputDirectory) }
    else { [IO.Path]::GetFullPath((Join-Path $taskRepository $OutputDirectory)) }
[IO.Directory]::CreateDirectory($taskOutput) | Out-Null
[IO.File]::WriteAllText((Join-Path $taskOutput 'ctest-list.json'), ($taskListing -join "`n"))
$taskReport = [ordered]@{ status = 'passed'; build_directory = $taskBuild;
    source_directory = $taskRepository; registered_tests = $taskNames;
    accepted_real_list = $true; rejected_missing_dependency_tools = $taskRejected }
[IO.File]::WriteAllText((Join-Path $taskOutput 'verification.json'), ($taskReport | ConvertTo-Json -Depth 5))
