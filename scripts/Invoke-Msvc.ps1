[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)] [string] $Command,
    [Parameter(ValueFromRemainingArguments = $true)] [string[]] $CommandArguments
)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer vswhere.exe was not found' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'An MSVC x64 toolchain was not found' }
$developerCommand = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $developerCommand)) { throw 'VsDevCmd.bat was not found' }
# Read the environment after initializing MSVC, then invoke the command directly in PowerShell.
# Arguments are never concatenated into a cmd.exe command string.
$developerEnvironment = & $env:ComSpec /d /c ('call "' + $developerCommand + '" -no_logo -arch=x64 -host_arch=x64 >nul && set')
if ($LASTEXITCODE -ne 0) { throw 'Initializing the MSVC environment failed' }
foreach ($line in $developerEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
& $Command @CommandArguments
exit $LASTEXITCODE
