[CmdletBinding()]
param(
    [string] $BuildDirectory = 'out/build/desktop-release',
    [string] $OutputDirectory = 'out/artifacts/windows',
    [string] $DependencySourceDirectory,
    [string] $InstallerCompiler,
    [switch] $SkipBuild,
    [switch] $SkipInstaller,
    [switch] $BootstrapInstaller,
    [int] $Jobs = 4
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT') { throw 'Windows packaging must run on Windows.' }
$taskRepository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Resolve-TaskPath([string] $Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $taskRepository $Path))
}
function Read-Cache([string] $Path) {
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^([^/#][^:]*):[^=]+=(.*)$') { $values[$Matches[1]] = $Matches[2] }
    }
    return $values
}
function Invoke-Checked([string] $Program, [string[]] $Arguments) {
    & $Program @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$Program exited with code $LASTEXITCODE" }
}
function Write-Json([string] $Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 20) + "`n", [Text.UTF8Encoding]::new($false))
}
function Read-PackageBuildRecord([string] $Path) {
    try {
        $json = Get-Content -Raw -LiteralPath $Path
        $record = $json | ConvertFrom-Json
    } catch {
        throw "Invalid build record '$Path': expected readable JSON from a completed build. Configure and fully build a new build directory; do not create or copy records to label old binaries. See packaging/windows/README.md."
    }
    # ConvertFrom-Json can unwrap a single-element array in older PowerShell.
    if ($null -eq $record -or $record -isnot [pscustomobject] -or $json -notmatch '^\s*\{') {
        throw "Invalid build record '$Path': expected a JSON object from a completed build. Configure and fully build a new build directory; do not create or copy records to label old binaries. See packaging/windows/README.md."
    }
    return $record
}
function Get-LibarchivePatch($Provenance, $Sources) {
    $backends = @($Sources.dependencies | Where-Object { $_.name -eq 'libarchive' })
    $hasPatch = $null -ne $Provenance.PSObject.Properties['libarchive_compatibility_patch']
    if ($backends.Count -eq 0 -and -not $hasPatch) { return $null }
    if ($backends.Count -ne 1 -or -not $hasPatch) {
        throw 'The bundled libarchive build requires its matching compatibility patch record.'
    }
    $record = $Provenance.libarchive_compatibility_patch
    if ($null -eq $record -or $record -isnot [pscustomobject] -or
        $null -eq $record.PSObject.Properties['path'] -or
        $null -eq $record.PSObject.Properties['sha256'] -or
        $null -eq $record.PSObject.Properties['upstream_revision'] -or
        $record.path -cne 'cmake/patches/libarchive-compatibility.patch' -or
        $record.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        $record.upstream_revision -cne $backends[0].revision) {
        throw 'The libarchive compatibility patch record differs from the resolved backend.'
    }
    $patch = Join-Path $taskRepository $record.path
    if (-not (Test-Path -LiteralPath $patch -PathType Leaf) -or
        (Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash.ToLowerInvariant() -cne $record.sha256) {
        throw 'The libarchive compatibility patch differs from its build record. Rebuild before packaging.'
    }
    return $patch
}
function Assert-X64Executable([string] $Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw 'The application is not a Windows executable.' }
        $stream.Position = 0x3C
        $header = $reader.ReadInt32()
        if ($header -lt 0 -or $header -gt $stream.Length - 6) { throw 'Invalid PE header in the application executable.' }
        $stream.Position = $header
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw 'The portable distribution requires a Windows x64 executable.'
        }
    } finally { $reader.Dispose() }
}
function Copy-QtNotices([string] $QtPrefix, [string] $Destination) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    $localLicenses = Join-Path $QtPrefix 'LICENSES'
    if (Test-Path -LiteralPath $localLicenses) {
        Copy-Item -LiteralPath $localLicenses -Destination $Destination -Recurse -Force
    }
    $sbomRoot = Join-Path $QtPrefix 'sbom'
    if (Test-Path -LiteralPath $sbomRoot) {
        Copy-Item -LiteralPath $sbomRoot -Destination $Destination -Recurse -Force
        foreach ($sbom in Get-ChildItem -LiteralPath $sbomRoot -Filter '*.spdx.json' -File) {
            $document = Get-Content -Raw -LiteralPath $sbom.FullName | ConvertFrom-Json
            if ($document.PSObject.Properties.Name -contains 'hasExtractedLicensingInfos') {
                foreach ($license in $document.hasExtractedLicensingInfos) {
                    if ($license.licenseId -notmatch '^[A-Za-z0-9_.+-]+$') { throw 'Unsafe Qt SPDX license identifier.' }
                    $target = Join-Path $Destination ('extracted-licenses/' + $license.licenseId + '.txt')
                    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
                    [IO.File]::WriteAllText($target, $license.extractedText, [Text.UTF8Encoding]::new($false))
                }
            }
            if (-not (Test-Path -LiteralPath $localLicenses)) {
                # Binary Qt kits carry source revisions in their vendor SBOM. Fetch
                # standard license texts from those exact official module sources.
                $module = @($document.packages | Where-Object {
                    $_.downloadLocation -match '^git://code\.qt\.io/qt/([A-Za-z0-9_-]+)\.git@([0-9a-f]{40})$'
                } | Select-Object -First 1)
                if ($module.Count -eq 0) { throw "Qt SBOM has no exact source revision: $($sbom.Name)" }
                $null = $module[0].downloadLocation -match '^git://code\.qt\.io/qt/([A-Za-z0-9_-]+)\.git@([0-9a-f]{40})$'
                $moduleName = $Matches[1]
                $revision = $Matches[2]
                $licenseCache = Join-Path $taskOutput ('.license-cache/qt/' + $moduleName + '/' + $revision)
                New-Item -ItemType Directory -Path $licenseCache -Force | Out-Null
                $expressions = @($document.packages + $document.files | ForEach-Object {
                    $_.PSObject.Properties | Where-Object { $_.Name -in @('licenseConcluded', 'licenseDeclared', 'licenseInfoInFile') } |
                        ForEach-Object { $_.Value }
                }) -join ' '
                $licenseIds = @([regex]::Matches($expressions, '[A-Za-z0-9][A-Za-z0-9_.+-]*') |
                    ForEach-Object Value | Where-Object { $_ -notin @('AND', 'OR', 'WITH', 'NONE', 'NOASSERTION') -and $_ -notlike 'LicenseRef-*' } |
                    Sort-Object -Unique)
                if ($licenseIds.Count -eq 0) { throw "Qt SBOM contains no standard license identifiers: $($sbom.Name)" }
                foreach ($licenseId in $licenseIds) {
                    $licenseFile = $licenseId + '.txt'
                    $cached = Join-Path $licenseCache $licenseFile
                    if (-not (Test-Path -LiteralPath $cached)) {
                        try {
                            Invoke-WebRequest -Uri ("https://raw.githubusercontent.com/qt/$moduleName/$revision/LICENSES/" + $licenseFile) -OutFile $cached -UseBasicParsing
                        } catch {
                            if (-not $_.Exception.Response -or [int]$_.Exception.Response.StatusCode -ne 404) { throw }
                            # Some SBOM standard licenses belong to nested bundled
                            # sources. SPDX publishes their canonical, immutable ID texts.
                            Invoke-WebRequest -Uri ("https://raw.githubusercontent.com/spdx/license-list-data/main/text/" + $licenseFile) -OutFile $cached -UseBasicParsing
                        }
                    }
                    $target = Join-Path $Destination ($moduleName + '/LICENSES/' + $licenseFile)
                    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
                    Copy-Item -LiteralPath $cached -Destination $target
                }
            }
        }
    }
    $installedNotices = @(Get-ChildItem -LiteralPath $QtPrefix -File -Recurse |
        Where-Object { $_.Name -match '^(LICENSE|COPYING|COPYRIGHT)([._-].*)?$|license.*\.txt$' -or $_.Directory.Name -eq 'LICENSES' })
    foreach ($notice in $installedNotices) {
        $relative = $notice.FullName.Substring($QtPrefix.Length + 1)
        $target = Join-Path $Destination ('installed-kit/' + $relative)
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $notice.FullName -Destination $target
    }
    if (-not (Test-Path -LiteralPath $localLicenses) -and -not (Test-Path -LiteralPath $sbomRoot)) {
        $sourceNotices = @($taskLicenseRecords | Where-Object { $_.name -eq 'qt/qtbase' -and $_.notices.Count -gt 0 })
        if ($taskProvenance.qt_source -ne 'bundled-source' -or $sourceNotices.Count -ne 1) {
            throw 'The selected Qt kit has neither installed notices nor a source-license provenance record.'
        }
    }
}
function Copy-QtPatchProvenance([string] $QtPrefix, [string] $ShareDirectory, [string] $LicenseDirectory) {
    $recordPath = Join-Path $QtPrefix 'share/AnAnArchiver/qt-windows-longpaths.json'
    $sourceBuild = $false
    if (Test-Path -LiteralPath $recordPath) {
        $record = Get-Content -Raw -LiteralPath $recordPath | ConvertFrom-Json
    } else {
        if ($taskProvenance.qt_source -ne 'bundled-source' -or
            $taskProvenance.PSObject.Properties.Name -notcontains 'qt_windows_longpaths_sha256' -or
            -not $taskProvenance.qt_windows_longpaths_sha256) { return $null }
        $sourceBuild = $true
        $record = [pscustomobject]@{ schema_version = 1; kind = 'coupled-source-build'; qt_version = $taskProvenance.qt_version;
            qtbase_revision = $taskProvenance.qtbase_revision; patch = 'cmake/patches/qt-windows-longpaths.patch';
            patch_sha256 = $taskProvenance.qt_windows_longpaths_sha256;
            patched_qtcore_sha256 = (Get-FileHash -LiteralPath (Join-Path $QtPrefix 'bin/Qt6Core.dll') -Algorithm SHA256).Hash.ToLowerInvariant();
            patched_qwindows_sha256 = (Get-FileHash -LiteralPath (Join-Path $QtPrefix 'plugins/platforms/qwindows.dll') -Algorithm SHA256).Hash.ToLowerInvariant();
            compiler_id = $taskProvenance.compiler_id; compiler_version = $taskProvenance.compiler_version }
        $sources = @($taskSources.dependencies | Where-Object { $_.name -eq 'qt/qtbase' -and $_.revision -eq $record.qtbase_revision })
        if ($sources.Count -ne 1) { throw 'The source Qt patch revision does not match its coupled source manifest.' }
    }
    if ($record.schema_version -ne 1 -or $record.qt_version -ne $taskProvenance.qt_version -or
        $record.patch -ne 'cmake/patches/qt-windows-longpaths.patch' -or
        $record.qtbase_revision -notmatch '^[0-9a-f]{40}$') {
        throw 'The modified Qt kit has an invalid or mismatched patch provenance record.'
    }
    $patch = Join-Path $taskRepository 'cmake/patches/qt-windows-longpaths.patch'
    $core = Join-Path $QtPrefix 'bin/Qt6Core.dll'
    $importLibrary = Join-Path $QtPrefix 'lib/Qt6Core.lib'
    if ((Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.patch_sha256 -or
        (Get-FileHash -LiteralPath $core -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.patched_qtcore_sha256 -or
        (-not $sourceBuild -and (Get-FileHash -LiteralPath $importLibrary -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.patched_import_library_sha256)) {
        throw 'The modified Qt patch, runtime or import library differs from its build record.'
    }
    if (-not $sourceBuild -and (-not $record.exports_names_and_ordinals_equal -or -not $record.core_features_equal -or
        $record.export_count -le 0 -or $record.required_import_count -le 0)) {
        throw 'The modified Qt kit has no successful feature/export/import compatibility validation.'
    }
    if ($record.PSObject.Properties.Name -contains 'patched_qwindows_sha256') {
        if ((Get-FileHash -LiteralPath (Join-Path $QtPrefix 'plugins/platforms/qwindows.dll') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.patched_qwindows_sha256) {
            throw 'The modified Qt Windows platform plugin differs from its build record.'
        }
        if (-not $sourceBuild -and (-not $record.qwindows_exports_names_and_ordinals_equal -or
            -not $record.qwindows_required_imports_verified -or -not $record.gui_features_equal)) {
            throw 'The modified Qt Windows platform plugin lacks successful feature/import/export validation.'
        }
    }
    if ($record.PSObject.Properties.Name -contains 'modified_binaries') {
        foreach ($component in $record.modified_binaries) {
            if ($component.relative_path -notin @('bin/Qt6Core.dll', 'plugins/platforms/qwindows.dll') -or
                (Get-FileHash -LiteralPath (Join-Path $QtPrefix $component.relative_path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $component.sha256) {
                throw 'A modified Qt component is unsafe or differs from its provenance hash.'
            }
        }
    }
    if (-not $sourceBuild) {
        $sbomPath = Join-Path $QtPrefix ('sbom/qtbase-' + $record.qt_version + '.spdx.json')
        $sbom = Get-Content -Raw -LiteralPath $sbomPath | ConvertFrom-Json
        $location = 'git://code.qt.io/qt/qtbase.git@' + $record.qtbase_revision
        if (-not @($sbom.packages | Where-Object { $_.name -eq 'qtbase' -and $_.downloadLocation -eq $location })) {
            throw 'The modified Qt kit source revision does not match the vendor QtBase SBOM.'
        }
        Copy-Item -LiteralPath $recordPath -Destination $ShareDirectory
    } else { Write-Json (Join-Path $ShareDirectory 'qt-windows-longpaths.json') $record }
    Copy-Item -LiteralPath $patch -Destination (Join-Path $ShareDirectory 'qt-windows-longpaths.patch')
    foreach ($recipe in @('Prepare-PatchedQtKit.ps1', 'prepare-patched-qt-kit.py')) {
        Copy-Item -LiteralPath (Join-Path (Join-Path $taskRepository 'scripts') $recipe) -Destination $ShareDirectory
    }
    $notice = @'
This distribution contains modified Qt components from the recorded Qt release.
The Windows DLL loader is changed to use extended-length absolute paths, and
GetModuleFileNameW uses a growing buffer. The Windows platform plugin only probes
OpenGL display selection for OpenGL surfaces; raster windows avoid this probe.
Unlisted Qt DLLs retain their vendor bytes. Modified components and their hashes
are identified in the build record.
Where supplied, vendor SBOM files describe the original release and notices; they
do not attest the rebuilt QtCore binary. The modification record, original and
modified hashes, patch, source revision, compiler, and compatibility checks are
in share/AnAnArchiver/qt-windows-longpaths.json and qt-windows-longpaths.patch.
The mirrored build recipe is supplied for traceability. Rebuild it from a full
LiteArc source checkout with scripts/Prepare-PatchedQtKit.ps1 and the matching
Qt source/build inputs recorded there. The original source is available from
https://code.qt.io/qt/qtbase.git at the recorded qtbase_revision.
Qt notices remain in licenses/qt-runtime or the coupled source-license directories.
Compatible replacements of Qt libraries remain supported.
'@
    if ($sourceBuild) {
        $notice = @'
This distribution contains Qt modules built together from the coupled release
sources in resolved-dependencies.json. QtCore includes the recorded Windows DLL
long-path loader correction. The QtBase revision, project patch hash, actual
QtCore/platform-plugin runtime hashes and compiler are recorded in qt-windows-longpaths.json.
No comparison with a separate vendor binary kit is claimed for this source build.
Rebuild from the full project checkout with the dependency resolution manifest;
cmake/PrepareQt.cmake applies the private-source patch during the superbuild.
Qt source license texts remain in the coupled source directories under licenses/.
The matching original QtBase source is https://code.qt.io/qt/qtbase.git at the
recorded qtbase_revision. Compatible Qt library replacements remain supported.
'@
    }
    [IO.File]::WriteAllText((Join-Path $LicenseDirectory 'Modified-Qt.txt'), $notice, [Text.UTF8Encoding]::new($false))
    return $record
}
function Get-LatestInno {
    # Resolve stable releases from the vendor's download page, not a pinned tag.
    $page = Invoke-WebRequest -Uri 'https://jrsoftware.org/isdl.php' -UseBasicParsing
    $candidates = @([regex]::Matches($page.Content,
        'https://github\.com/jrsoftware/issrc/releases/download/[^"<> ]+/innosetup-(\d+\.\d+\.\d+)-x64\.exe') |
        ForEach-Object { [pscustomobject]@{ version = $_.Groups[1].Value; url = $_.Value } } |
        Sort-Object { [version]$_.version } -Descending)
    if ($candidates.Count -eq 0) { throw 'No stable x64 Inno Setup release was found on the official download page.' }
    return $candidates[0]
}
function Get-InnoCompiler {
    $latest = Get-LatestInno
    $candidate = $InstallerCompiler
    $toolRoot = Join-Path $env:LOCALAPPDATA ("AnAnArchiver/BuildTools/InnoSetup/" + $latest.version)
    if (-not $candidate) {
        $choices = @((Join-Path $toolRoot 'ISCC.exe'),
            (Join-Path $env:ProgramFiles 'Inno Setup 7/ISCC.exe'),
            (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 7/ISCC.exe'))
        $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($command) { $choices += $command.Source }
        $candidate = $choices | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    }
    if (-not $candidate -and $BootstrapInstaller) {
        New-Item -ItemType Directory -Path $toolRoot -Force | Out-Null
        $download = Join-Path $toolRoot ('innosetup-' + $latest.version + '-x64.exe')
        Invoke-WebRequest -Uri $latest.url -OutFile $download -UseBasicParsing
        $signature = Get-AuthenticodeSignature -LiteralPath $download
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=Pyrsys B\.V\.') {
            throw 'The official Inno Setup download has no valid expected publisher signature.'
        }
        $process = Start-Process -FilePath $download -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES',
            '/NORESTART', '/CURRENTUSER', '/NOICONS', ('/DIR="' + $toolRoot + '"')) -WindowStyle Hidden -PassThru -Wait
        if ($process.ExitCode -ne 0) { throw "Installing the Inno Setup build tool failed: $($process.ExitCode)" }
        $candidate = Join-Path $toolRoot 'ISCC.exe'
    }
    if (-not $candidate) {
        throw 'Install the latest stable Inno Setup 7 compiler, pass -InstallerCompiler, or use -BootstrapInstaller.'
    }
    $candidate = (Resolve-Path -LiteralPath $candidate).Path
    $versionText = (& $candidate --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $versionText -notmatch '\d+\.\d+\.\d+') { throw "Cannot identify the Inno Setup compiler version: $versionText" }
    $actual = $Matches[0]
    if ([version]$actual -ne [version]$latest.version) {
        throw "Inno Setup $actual is not the current stable release $($latest.version). Update the build tool."
    }
    return [pscustomobject]@{ path = $candidate; version = $actual; source = $latest.url }
}

$taskBuild = Resolve-TaskPath $BuildDirectory
$taskOutput = Resolve-TaskPath $OutputDirectory
if (-not $SkipBuild) { Invoke-Checked 'cmake' @('--build', $taskBuild, '--parallel', "$Jobs") }
$taskApplication = $taskBuild
if (-not (Test-Path -LiteralPath (Join-Path $taskApplication 'LiteArc.exe'))) {
    $taskApplication = Join-Path $taskBuild 'application'
}
$taskExecutable = Join-Path $taskApplication 'LiteArc.exe'
if (-not (Test-Path -LiteralPath $taskExecutable)) { throw "Missing release executable: $taskExecutable" }
# Both records belong to the application build, even when a superbuild directory
# was supplied. Validate them before staging cleanup, deployment or tool downloads.
$taskProvenancePath = Join-Path $taskApplication 'dependency-provenance.json'
$taskManifestPath = Join-Path $taskApplication 'resolved-dependencies.json'
$taskMissingRecords = @($taskProvenancePath, $taskManifestPath | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
if ($taskMissingRecords.Count -gt 0) {
    throw @"
Cannot package this build: required build records are missing:
$($taskMissingRecords -join "`n")
-SkipBuild only packages a Release build completed by the current dependency-traceable build workflow. An existing executable without these records has no verified dependency provenance.
Configure and fully build a new build directory (see packaging/windows/README.md for MSVC source-Qt commands). Do not create or copy records to label old binaries or dependencies.
"@
}
$taskProvenance = Read-PackageBuildRecord $taskProvenancePath
$taskSources = Read-PackageBuildRecord $taskManifestPath
$taskLibarchivePatch = Get-LibarchivePatch $taskProvenance $taskSources
Assert-X64Executable $taskExecutable
$taskCache = Read-Cache (Join-Path $taskApplication 'CMakeCache.txt')
if ($taskCache['CMAKE_BUILD_TYPE'] -ne 'Release') { throw 'Portable packages require a Release application build.' }
if (-not $taskCache['Qt6Core_DIR']) { throw 'The application cache does not identify its Qt kit.' }
$taskQtPrefix = [IO.Path]::GetFullPath((Join-Path $taskCache['Qt6Core_DIR'] '../../..'))
$taskQtBin = Join-Path $taskQtPrefix 'bin'
$taskDeploy = Join-Path $taskQtBin 'windeployqt.exe'
if (-not (Test-Path -LiteralPath $taskDeploy)) { throw "The selected Qt kit has no windeployqt: $taskDeploy" }
if ($taskProvenance.compiler_id -ne 'MSVC') { throw 'This Windows release packager requires the MSVC x64 kit.' }
if ($taskCache['CMAKE_SIZEOF_VOID_P'] -and $taskCache['CMAKE_SIZEOF_VOID_P'] -ne '8') {
    throw 'This release packager produces Windows x64 artifacts only.'
}
$taskVersion = $taskProvenance.project_version
if ($taskVersion -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') { throw 'The project version cannot be used as an installer version.' }
$taskName = "LiteArc-$taskVersion-windows-x64"
$taskStage = Join-Path $taskOutput $taskName
$taskZip = Join-Path $taskOutput ($taskName + '.zip')
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
# Only the fixed child package directory may be cleaned; never delete a supplied root.
if ([IO.Path]::GetDirectoryName($taskStage) -ne $taskOutput -or [IO.Path]::GetFileName($taskStage) -ne $taskName) {
    throw 'The package staging directory escaped the selected artifact directory.'
}
if (Test-Path -LiteralPath $taskStage) { Remove-Item -LiteralPath $taskStage -Recurse -Force }
New-Item -ItemType Directory -Path $taskStage | Out-Null
Copy-Item -LiteralPath $taskExecutable -Destination $taskStage
# A dynamically built backend is packaged too; the default backend/codec build is static.
Get-ChildItem -LiteralPath $taskApplication -Filter '*.dll' -File | Where-Object { $_.Name -notlike 'Qt6*' -and $_.Name -ne 'opengl32sw.dll' } |
    Copy-Item -Destination $taskStage
$taskOldPath = $env:PATH
try {
    $env:PATH = $taskQtBin + [IO.Path]::PathSeparator + $env:PATH
    Invoke-Checked (Join-Path $PSScriptRoot 'Invoke-Msvc.ps1') @($taskDeploy, '--release', '--force', '--no-patchqt', '--verbose', '0',
        '--compiler-runtime', '--no-opengl-sw', '--qmldir', (Join-Path $taskRepository 'frontend/qt/qml'),
        '--plugindir', (Join-Path $taskStage 'plugins'),
        '--svg', '--include-plugins', 'qsvg,qsvgicon',
        '--qtpaths', (Join-Path $taskQtBin 'qtpaths.exe'), '--no-translations', (Join-Path $taskStage 'LiteArc.exe'))
} finally { $env:PATH = $taskOldPath }

# The portable ZIP must run without installing the centrally deployed VC runtime.
# Copy only Microsoft redistributable CRT binaries, never DLLs from System32.
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $taskVs) { throw 'No MSVC redistributable installation was found.' }
$taskRedistRoot = Join-Path $taskVs 'VC/Redist/MSVC'
$taskRedistVersion = Get-ChildItem -LiteralPath $taskRedistRoot -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $taskRedistVersion) { throw 'The Visual Studio x64 redistributable source is missing.' }
$taskCrt = Get-ChildItem -LiteralPath (Join-Path $taskRedistVersion.FullName 'x64') -Directory |
    Where-Object { $_.Name -match '^Microsoft\.VC\d+\.CRT$' } | Select-Object -First 1
if (-not $taskCrt) { throw 'The MSVC release CRT redistributable source is missing.' }
$taskRuntimeRecords = @()
foreach ($dll in Get-ChildItem -LiteralPath $taskCrt.FullName -Filter '*.dll' -File) {
    Copy-Item -LiteralPath $dll.FullName -Destination $taskStage -Force
    $taskRuntimeRecords += [ordered]@{ relative_path = $dll.Name; source_path = $dll.FullName;
        sha256 = (Get-FileHash -LiteralPath $dll.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
foreach ($required in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'Qt6Core.dll', 'Qt6Quick.dll', 'Qt6Svg.dll',
    'plugins/platforms/qwindows.dll', 'plugins/imageformats/qsvg.dll', 'plugins/iconengines/qsvgicon.dll', 'qml/QtQuick/qmldir')) {
    if (-not (Test-Path -LiteralPath (Join-Path $taskStage $required))) { throw "Incomplete deployment: $required" }
}
[IO.File]::WriteAllText((Join-Path $taskStage 'qt.conf'), "[Paths]`nPrefix=.`nPlugins=plugins`nQmlImports=qml`n", [Text.UTF8Encoding]::new($false))
New-Item -ItemType Directory -Path (Join-Path $taskStage 'share/AnAnArchiver') -Force | Out-Null
foreach ($record in @('dependency-provenance.json', 'resolved-dependencies.json')) {
    Copy-Item -LiteralPath (Join-Path $taskApplication $record) -Destination (Join-Path $taskStage 'share/AnAnArchiver')
}
if ($taskLibarchivePatch) {
    Copy-Item -LiteralPath $taskLibarchivePatch -Destination (Join-Path $taskStage 'share/AnAnArchiver/libarchive-compatibility.patch')
}

if (-not $DependencySourceDirectory) {
    $taskManifestCachePath = $taskCache['ANANARCHIVER_DEPENDENCY_MANIFEST']
    if ($taskManifestCachePath -and (Test-Path -LiteralPath $taskManifestCachePath)) {
        $DependencySourceDirectory = [IO.Path]::GetFullPath((Join-Path (Split-Path $taskManifestCachePath -Parent) '../..'))
    } else { $DependencySourceDirectory = $taskRepository }
}
$taskDependencySources = Resolve-TaskPath $DependencySourceDirectory
$taskLicenseRecords = @()
$taskLicenses = Join-Path $taskStage 'licenses'
New-Item -ItemType Directory -Path $taskLicenses | Out-Null
foreach ($dependency in $taskSources.dependencies) {
    if ($dependency.path -notmatch '^third_party/[A-Za-z0-9_.\/-]+$' -or $dependency.path -match '(^|/)\.\.(/|$)') {
        throw 'Unsafe dependency source path in provenance.'
    }
    if ($dependency.name -notmatch '^[A-Za-z0-9_.\/-]+$' -or $dependency.name -match '(^|/)\.\.(/|$)') {
        throw 'Unsafe dependency name in provenance.'
    }
    $sourceRoot = Join-Path $taskDependencySources $dependency.path
    if (-not (Test-Path -LiteralPath $sourceRoot)) { throw "License source tree unavailable: $sourceRoot" }
    $files = @(Get-ChildItem -LiteralPath $sourceRoot -File -Recurse |
        Where-Object { $_.Name -match '^(LICENSE|COPYING|COPYRIGHT)([._-].*)?$|license.*\.txt$' -or $_.Directory.Name -eq 'LICENSES' })
    if ($files.Count -eq 0) { throw "No license notices were found for $($dependency.name)." }
    $copied = @()
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($sourceRoot.Length + 1)
        $target = Join-Path $taskLicenses (Join-Path $dependency.name $relative)
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
        $copied += ('licenses/' + $dependency.name + '/' + $relative.Replace('\', '/'))
    }
    $taskLicenseRecords += [ordered]@{ name = $dependency.name; version = $dependency.version;
        revision = $dependency.revision; source = $dependency.url; notices = $copied }
}
Copy-Item -LiteralPath (Join-Path $taskRepository 'frontend/qt/qml/icons/LICENSE.txt') -Destination (Join-Path $taskLicenses 'icons.txt')
Copy-QtNotices $taskQtPrefix (Join-Path $taskLicenses 'qt-runtime')
$taskQtPatch = Copy-QtPatchProvenance $taskQtPrefix (Join-Path $taskStage 'share/AnAnArchiver') $taskLicenses
$taskRuntimeNotice = @'
Microsoft Visual C++ runtime binaries are redistributed from the licensed Visual
Studio REDIST directory, as app-local release CRT files. They are governed by the
Microsoft Visual Studio Software License Terms and its Distributable Code list.
https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files
https://visualstudio.microsoft.com/license-terms/
Windows Universal CRT components are supplied by supported Windows 10/11.
'@
[IO.File]::WriteAllText((Join-Path $taskLicenses 'Microsoft-Visual-Cpp-runtime.txt'), $taskRuntimeNotice, [Text.UTF8Encoding]::new($false))
Write-Json (Join-Path $taskStage 'share/AnAnArchiver/third-party-notices.json') $taskLicenseRecords
$taskNotices = @'
LiteArc uses open source archive codecs and dynamically linked Qt libraries.
Dependency versions, revisions, source URLs and license texts are included in
share/AnAnArchiver and licenses/. Qt libraries may be replaced with compatible
modified builds; the package does not restrict debugging those modifications.
Application source and build instructions: https://github.com/ananwanan/AnAnArchiver
Backend source: https://github.com/libarchive/libarchive
Bundled backend builds apply the recorded compatibility fixes in
share/AnAnArchiver/libarchive-compatibility.patch; its hash and official base
revision are in dependency-provenance.json. Apply this patch after replaying
the source manifest to reproduce the modified backend.
For the exact dependency sources, use the included resolved-dependencies.json
with scripts/resolve-dependencies.py --replay-manifest; see the repository build guidance.
The Qt runtime's vendor SBOM and matching license texts are in licenses/qt-runtime.
'@
[IO.File]::WriteAllText((Join-Path $taskStage 'THIRD-PARTY-NOTICES.txt'), $taskNotices, [Text.UTF8Encoding]::new($false))

$taskQtRecords = @()
foreach ($dll in Get-ChildItem -LiteralPath $taskStage -Filter '*.dll' -Recurse -File) {
    if ($dll.Name -match '^(msvcp|vcruntime|concrt)') { continue }
    $relative = $dll.FullName.Substring($taskStage.Length + 1)
    $source = Join-Path $taskQtBin $dll.Name
    if ($relative -match '^qml\\') { $source = Join-Path $taskQtPrefix $relative }
    elseif ($relative -match '^plugins\\') { $source = Join-Path $taskQtPrefix $relative }
    elseif (-not (Test-Path -LiteralPath $source)) { $source = Join-Path (Join-Path $taskQtPrefix 'plugins') $relative }
    if (-not (Test-Path -LiteralPath $source)) { continue }
    $hash = (Get-FileHash -LiteralPath $dll.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $sourceHash) { throw "Deployed DLL differs from the selected Qt kit: $relative" }
    $taskQtRecords += [ordered]@{ relative_path = $relative.Replace('\', '/'); source_path = $source; sha256 = $hash }
}
if ($taskQtRecords.Count -lt 5) { throw 'The deployment could not be verified against the configured Qt kit.' }
$taskInno = $null
if (-not $SkipInstaller) {
    $taskInno = Get-InnoCompiler
    Copy-Item -LiteralPath (Join-Path (Split-Path $taskInno.path -Parent) 'license.txt') -Destination (Join-Path $taskLicenses 'Inno-Setup.txt')
}
$taskOwnedFiles = @(Get-ChildItem -LiteralPath $taskStage -File -Recurse | Sort-Object FullName | ForEach-Object {
    $_.FullName.Substring($taskStage.Length + 1).Replace('\', '/') + '|' +
        (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
})
[IO.File]::WriteAllLines((Join-Path $taskStage 'package-owned-files.txt'), $taskOwnedFiles, [Text.UTF8Encoding]::new($false))
$taskFiles = @(Get-ChildItem -LiteralPath $taskStage -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{ relative_path = $_.FullName.Substring($taskStage.Length + 1).Replace('\', '/'); size = $_.Length;
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
$taskPackageManifest = [ordered]@{ schema_version = 1; project_version = $taskVersion; platform = 'windows-x64';
    created_utc = [DateTime]::UtcNow.ToString('o'); build = $taskProvenance;
    deployment = [ordered]@{ qt_prefix = $taskQtPrefix; qt_dlls = $taskQtRecords;
        compiler_runtime_source = $taskCrt.FullName; compiler_runtime_dlls = $taskRuntimeRecords;
        qt_windows_longpaths_patch = $taskQtPatch };
    installer_compiler = $taskInno; files = $taskFiles }
Write-Json (Join-Path $taskStage 'package-manifest.json') $taskPackageManifest
if (Test-Path -LiteralPath $taskZip) { Remove-Item -LiteralPath $taskZip -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($taskStage, $taskZip, [IO.Compression.CompressionLevel]::Optimal, $true)
if ($taskInno) {
    Invoke-Checked $taskInno.path @('/Qp', ('/DPackageRoot=' + $taskStage), ('/DOutputRoot=' + $taskOutput),
        ('/DAppVersion=' + $taskVersion), ('/DOutputName=' + $taskName + '-setup'),
        (Join-Path $taskRepository 'packaging/windows/AnAnArchiver.iss'))
}
$taskArtifacts = @($taskZip)
if ($taskInno) { $taskArtifacts += (Join-Path $taskOutput ($taskName + '-setup.exe')) }
Write-Json (Join-Path $taskOutput ($taskName + '-artifacts.json')) @($taskArtifacts | ForEach-Object {
    [ordered]@{ path = $_; sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant() }
})
Write-Host "Portable directory: $taskStage"
Write-Host "Portable ZIP: $taskZip"
if ($taskInno) { Write-Host "Installer: $($taskArtifacts[1])" }
