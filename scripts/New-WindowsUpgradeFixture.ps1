[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $PortableDirectory,
    [string] $OutputDirectory,
    [string] $InstallerCompiler,
    [string] $PreviousVersion = '0.9.0'
)
$ErrorActionPreference = 'Stop'
$taskRepository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskPortable = (Resolve-Path -LiteralPath $PortableDirectory).Path
$taskManifest = Get-Content -Raw -LiteralPath (Join-Path $taskPortable 'package-manifest.json') | ConvertFrom-Json
if ($PreviousVersion -notmatch '^\d+\.\d+\.\d+(\.\d+)?$' -or
    [version]$PreviousVersion -ge [version]$taskManifest.project_version) {
    throw 'The upgrade fixture version must be a valid version older than the current package.'
}
if (-not $OutputDirectory) { $OutputDirectory = Split-Path $taskPortable -Parent }
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $InstallerCompiler) { $InstallerCompiler = $taskManifest.installer_compiler.path }
if (-not $InstallerCompiler -or -not (Test-Path -LiteralPath $InstallerCompiler)) {
    throw 'The current package has no available Inno compiler; pass -InstallerCompiler.'
}
$taskCompilerVersion = (& $InstallerCompiler --version | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $taskCompilerVersion -notmatch '\d+\.\d+\.\d+' -or
    $Matches[0] -ne $taskManifest.installer_compiler.version) {
    throw 'The fixture must use the same stable Inno compiler as the release package.'
}
$taskFixtureRoot = Join-Path $taskRepository 'out/fixtures'
$taskFixtureName = 'windows-upgrade-' + $PreviousVersion
$taskFixture = Join-Path $taskFixtureRoot $taskFixtureName
if ([IO.Path]::GetDirectoryName($taskFixture) -ne [IO.Path]::GetFullPath($taskFixtureRoot)) {
    throw 'Unsafe upgrade fixture directory.'
}
if (Test-Path -LiteralPath $taskFixture) { Remove-Item -LiteralPath $taskFixture -Recurse -Force }
New-Item -ItemType Directory -Path $taskFixture -Force | Out-Null
Get-ChildItem -LiteralPath $taskPortable | Copy-Item -Destination $taskFixture -Recurse
# The app payload is current; only the installer version and obsolete managed-file
# fixtures differ. This is a version-transition test, not historical app compatibility.
foreach ($name in @('obsolete-component.txt', 'user-modified-managed.txt')) {
    $path = Join-Path $taskFixture $name
    [IO.File]::WriteAllText($path, 'Original managed upgrade fixture: ' + $name)
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::AppendAllText((Join-Path $taskFixture 'package-owned-files.txt'),
        $name + '|' + $hash + "`n", [Text.UTF8Encoding]::new($false))
}
$taskOutputName = 'AnAnArchiver-' + $PreviousVersion + '-windows-x64-upgrade-fixture-setup'
$taskCompilerArguments = @('/Qp', ('/DPackageRoot=' + $taskFixture), ('/DOutputRoot=' + $taskOutput),
    ('/DAppVersion=' + $PreviousVersion), ('/DOutputName=' + $taskOutputName),
    (Join-Path $taskRepository 'packaging/windows/AnAnArchiver.iss'))
& $InstallerCompiler @taskCompilerArguments
if ($LASTEXITCODE -ne 0) { throw 'Compiling the upgrade fixture installer failed.' }
Write-Host ('Upgrade fixture installer: ' + (Join-Path $taskOutput ($taskOutputName + '.exe')))
