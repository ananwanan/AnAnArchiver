[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $BuildDirectory,
    [string] $TemporaryRoot,
    [int] $Jobs = 4
)
$ErrorActionPreference = 'Stop'
$taskBuildPath = (Resolve-Path -LiteralPath $BuildDirectory).Path
$taskOldTemp = $env:TEMP
$taskOldTmp = $env:TMP
$taskOldTestRoot = $env:ANANARCHIVER_TEST_TEMP_ROOT
try {
    if ($TemporaryRoot) {
        New-Item -ItemType Directory -Path $TemporaryRoot -Force | Out-Null
        $taskTestPath = (Resolve-Path -LiteralPath $TemporaryRoot).Path
        $env:TEMP = $taskTestPath
        $env:TMP = $taskTestPath
        $env:ANANARCHIVER_TEST_TEMP_ROOT = $taskTestPath
    }
    & ctest --test-dir $taskBuildPath --output-on-failure --parallel $Jobs
    $taskResult = $LASTEXITCODE
} finally {
    $env:TEMP = $taskOldTemp
    $env:TMP = $taskOldTmp
    $env:ANANARCHIVER_TEST_TEMP_ROOT = $taskOldTestRoot
}
exit $taskResult
