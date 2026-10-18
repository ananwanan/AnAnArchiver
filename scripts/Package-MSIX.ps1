<#
.SYNOPSIS
Packages the Release MSVC x64 desktop application for Microsoft Store submission.
.DESCRIPTION
Uses Package-Windows.ps1 for deployment, or verifies an existing portable directory.
Supply the three identity values from Partner Center. The resulting MSIX is unsigned;
the Store signs it after certification. No certificate or account credentials are used.
#>
[CmdletBinding(DefaultParameterSetName = 'Build')]
param(
    [Parameter(ParameterSetName = 'Build')] [string] $BuildDirectory = 'out/build/desktop-release',
    [Parameter(ParameterSetName = 'Build')] [string] $DependencySourceDirectory,
    [Parameter(ParameterSetName = 'Build')] [switch] $SkipBuild,
    [Parameter(ParameterSetName = 'Build')] [ValidateRange(1, 256)] [int] $Jobs = 4,
    [Parameter(Mandatory = $true, ParameterSetName = 'Portable')] [string] $PortableDirectory,
    [Parameter(Mandatory = $true)] [ValidateNotNullOrEmpty()] [string] $IdentityName,
    [Parameter(Mandatory = $true)] [ValidateNotNullOrEmpty()] [string] $Publisher,
    [Parameter(Mandatory = $true)] [ValidateNotNullOrEmpty()] [string] $PublisherDisplayName,
    [string] $DisplayName = 'LiteArc',
    [string] $PackageVersion,
    [string] $OutputDirectory = 'out/artifacts/msix',
    [string] $MakeAppxPath
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT') { throw 'MSIX packaging must run on Windows.' }
$taskRepository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Resolve-TaskPath([string] $Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { throw 'An empty filesystem path is not allowed.' }
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path).TrimEnd('\', '/') }
    return [IO.Path]::GetFullPath((Join-Path $taskRepository $Path)).TrimEnd('\', '/')
}
function Read-JsonObject([string] $Path) {
    try {
        $json = [IO.File]::ReadAllText($Path)
        $value = $json | ConvertFrom-Json
        if ($json -notmatch '^\s*\{' -or $value -isnot [pscustomobject]) { throw 'Expected a JSON object.' }
        return $value
    } catch { throw "Invalid or missing '$Path': $($_.Exception.Message)" }
}
function Write-Json([string] $Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 30) + "`n", [Text.UTF8Encoding]::new($false))
}
function Get-StoreVersion([string] $Value) {
    if ($Value -notmatch '^\d+\.\d+\.\d+\.\d+$') {
        throw 'PackageVersion must contain four integers: major.minor.build.0.'
    }
    $parts = @($Value.Split('.') | ForEach-Object {
        $number = 0
        if (-not [int]::TryParse($_, [ref] $number) -or $number -gt 65535) {
            throw 'PackageVersion components must be between 0 and 65535.'
        }
        $number
    })
    if ($parts[0] -eq 0 -or $parts[3] -ne 0) {
        throw 'PackageVersion requires a nonzero major component and a final component of 0 for Microsoft Store.'
    }
    return $parts -join '.'
}
function Assert-ArtifactPathsAvailable([string] $Version) {
    $name = "LiteArc-$Version-windows-x64"
    foreach ($suffix in @('.msix', '.AppxManifest.xml', '-artifacts.json')) {
        $path = Join-Path $taskOutput ($name + $suffix)
        if (Test-Path -LiteralPath $path) {
            throw "Existing MSIX artifact: $path. Choose a new PackageVersion or OutputDirectory; existing release artifacts are never overwritten."
        }
    }
}
function Assert-X64Executable([string] $Path) {
    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead($Path))
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw 'Invalid LiteArc.exe DOS header.' }
        $reader.BaseStream.Position = 0x3C
        $offset = $reader.ReadInt32()
        if ($offset -lt 0 -or $offset -gt $reader.BaseStream.Length - 6) { throw 'Invalid LiteArc.exe PE offset.' }
        $reader.BaseStream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw 'LiteArc.exe must be a Windows x64 executable.'
        }
    } finally { $reader.Dispose() }
}
function Assert-PortablePackage([string] $Root) {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { throw "Missing PortableDirectory: $Root" }
    # Reject links before reading/hash verification or copying can follow them.
    $items = @((Get-Item -LiteralPath $Root)) + @(Get-ChildItem -LiteralPath $Root -Recurse -Force)
    foreach ($item in $items) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse path in PortableDirectory: $($item.FullName)" }
    }
    $manifest = Read-JsonObject (Join-Path $Root 'package-manifest.json')
    if ($manifest.schema_version -ne 1) { throw 'Unsupported package-manifest.json schema_version.' }
    if ($manifest.platform -cne 'windows-x64') { throw 'package-manifest.json platform must be windows-x64.' }
    if ($manifest.build.compiler_id -cne 'MSVC') { throw 'package-manifest.json requires an MSVC build.' }
    if ($manifest.project_version -cne $manifest.build.project_version) { throw 'package-manifest.json project_version differs from its build record.' }
    if ($manifest.build.toolchain.build_type -cne 'Release') { throw 'package-manifest.json requires a Release toolchain.build_type.' }
    $provenance = Read-JsonObject (Join-Path $Root 'share/AnAnArchiver/dependency-provenance.json')
    $null = Read-JsonObject (Join-Path $Root 'share/AnAnArchiver/resolved-dependencies.json')
    if (($provenance | ConvertTo-Json -Depth 30 -Compress) -cne ($manifest.build | ConvertTo-Json -Depth 30 -Compress)) {
        throw 'dependency-provenance.json differs from the portable package build record.'
    }
    $known = @{}
    foreach ($file in $manifest.files) {
        $relative = [string] $file.relative_path
        if ([string]::IsNullOrWhiteSpace($relative) -or $relative -match '(^/|\\|:|(^|/)\.\.?(/|$))' -or
            $relative -eq 'package-manifest.json' -or $known.ContainsKey($relative)) {
            throw "Unsafe or duplicate package-manifest.json file path: $relative"
        }
        $path = [IO.Path]::GetFullPath((Join-Path $Root $relative))
        if (-not $path.StartsWith($Root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing or unsafe portable file: $relative" }
        if ($file.sha256 -cnotmatch '^[0-9a-f]{64}$' -or (Get-Item -LiteralPath $path).Length -ne $file.size -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $file.sha256) {
            throw "Portable file differs from package-manifest.json: $relative"
        }
        $known[$relative] = $true
    }
    foreach ($file in $items | Where-Object { -not $_.PSIsContainer }) {
        $relative = $file.FullName.Substring($Root.Length + 1).Replace('\', '/')
        if ($relative -ne 'package-manifest.json' -and -not $known.ContainsKey($relative)) {
            throw "Unlisted portable file: $relative"
        }
    }
    foreach ($required in @('LiteArc.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Qml.dll', 'Qt6Quick.dll', 'Qt6Svg.dll',
        'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'plugins/platforms/qwindows.dll',
        'plugins/imageformats/qsvg.dll', 'plugins/iconengines/qsvgicon.dll', 'qml/QtQuick/qmldir', 'qt.conf',
        'THIRD-PARTY-NOTICES.txt', 'share/AnAnArchiver/third-party-notices.json',
        'share/AnAnArchiver/dependency-provenance.json', 'share/AnAnArchiver/resolved-dependencies.json')) {
        if (-not $known.ContainsKey($required)) { throw "Incomplete portable package: $required" }
    }
    Assert-X64Executable (Join-Path $Root 'LiteArc.exe')
    return $manifest
}
function Find-MakeAppx {
    if ($MakeAppxPath) {
        $path = Resolve-TaskPath $MakeAppxPath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing MakeAppxPath: $path" }
        return $path
    }
    $roots = @($env:WindowsSdkDir)
    foreach ($key in @('HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots')) {
        $record = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        if ($record -and $record.PSObject.Properties['KitsRoot10']) { $roots += $record.KitsRoot10 }
    }
    if (${env:ProgramFiles(x86)}) { $roots += Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10' }
    $candidates = @(foreach ($root in $roots | Where-Object { $_ } | Select-Object -Unique) {
        $bin = Join-Path $root 'bin'
        if (-not (Test-Path -LiteralPath $bin)) { continue }
        foreach ($directory in Get-ChildItem -LiteralPath $bin -Directory) {
            if ($directory.Name -notmatch '^10\.0\.\d+\.0$') { continue }
            $tool = Join-Path $directory.FullName 'x64/makeappx.exe'
            if (Test-Path -LiteralPath $tool -PathType Leaf) { [pscustomobject]@{ path = $tool; version = [version] $directory.Name } }
        }
    })
    if ($candidates.Count -gt 0) { return ($candidates | Sort-Object version -Descending | Select-Object -First 1).path }
    $command = Get-Command makeappx.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw 'MakeAppx.exe was not found. Install the Windows SDK MSIX packaging tools, or pass -MakeAppxPath.'
}
function Invoke-MakeAppx([string[]] $Arguments) {
    $output = @(& $taskMakeAppx @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "MakeAppx failed ($LASTEXITCODE):`n$($output -join "`n")" }
    Write-Verbose ($output -join "`n")
}
function New-LogoAssets([string] $Destination) {
    Add-Type -AssemblyName System.Drawing
    $source = [Drawing.Image]::FromFile((Join-Path $taskRepository 'frontend/qt/resources/icons/ananarchiver.png'))
    try {
        New-Item -ItemType Directory -Path $Destination | Out-Null
        foreach ($size in @(44, 50, 150)) {
            $bitmap = [Drawing.Bitmap]::new($size, $size)
            $graphics = $null
            try {
                $graphics = [Drawing.Graphics]::FromImage($bitmap)
                $graphics.Clear([Drawing.Color]::Transparent)
                $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.DrawImage($source, 0, 0, $size, $size)
                $bitmap.Save((Join-Path $Destination ("Logo$size.png")), [Drawing.Imaging.ImageFormat]::Png)
            } finally {
                if ($graphics) { $graphics.Dispose() }
                $bitmap.Dispose()
            }
        }
    } finally { $source.Dispose() }
}

# Validate Store configuration before building, looking up SDK tools or changing outputs.
if ($IdentityName -cnotmatch '^[A-Za-z0-9.-]{3,50}$' -or $IdentityName -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)') {
    throw 'IdentityName must be the Partner Center package name (3-50 letters, digits, dots or hyphens; no reserved device name).'
}
try { $null = [Security.Cryptography.X509Certificates.X500DistinguishedName]::new($Publisher) }
catch { throw 'Publisher must be the X.500 distinguished name from Partner Center.' }
if ($Publisher -notmatch '(^|,\s*)CN\s*=\s*\S') { throw 'Publisher must include the Partner Center CN value.' }
foreach ($field in @(@('PublisherDisplayName', $PublisherDisplayName), @('DisplayName', $DisplayName))) {
    if ([string]::IsNullOrWhiteSpace($field[1]) -or $field[1].Length -gt 256 -or $field[1] -match '[\x00-\x1F]') {
        throw "$($field[0]) must contain 1-256 visible characters."
    }
}
if ($PSBoundParameters.ContainsKey('PackageVersion')) { $PackageVersion = Get-StoreVersion $PackageVersion }
$taskOutput = Resolve-TaskPath $OutputDirectory
$taskPortable = $null
$taskPortableManifest = $null
if ($PSCmdlet.ParameterSetName -eq 'Portable') {
    $taskPortable = Resolve-TaskPath $PortableDirectory
    if ($taskOutput -eq $taskPortable -or $taskOutput.StartsWith($taskPortable + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'OutputDirectory must be outside PortableDirectory.'
    }
    $taskPortableManifest = Assert-PortablePackage $taskPortable
    if (-not $PackageVersion) {
        $sourceVersion = [string] $taskPortableManifest.project_version
        if ($sourceVersion -match '^\d+\.\d+\.\d+$') { $sourceVersion += '.0' }
        $PackageVersion = Get-StoreVersion $sourceVersion
    }
}
if ($PackageVersion) { Assert-ArtifactPathsAvailable $PackageVersion }
# Tool preflight precedes expensive deployment. Portable validation precedes tool lookup.
$taskMakeAppx = Find-MakeAppx
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskWork = Join-Path $taskOutput ('.msix-work-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskWork | Out-Null
try {
    if (-not $taskPortable) {
        $arguments = @{ BuildDirectory = $BuildDirectory; OutputDirectory = (Join-Path $taskWork 'portable');
            SkipBuild = $SkipBuild; SkipInstaller = $true; Jobs = $Jobs }
        if ($DependencySourceDirectory) { $arguments.DependencySourceDirectory = $DependencySourceDirectory }
        & (Join-Path $PSScriptRoot 'Package-Windows.ps1') @arguments
        $directories = @(Get-ChildItem -LiteralPath $arguments.OutputDirectory -Directory -Filter 'LiteArc-*-windows-x64')
        if ($directories.Count -ne 1) { throw 'Windows deployment did not produce exactly one portable package.' }
        $taskPortable = $directories[0].FullName
        $taskPortableManifest = Assert-PortablePackage $taskPortable
    }
    if (-not $PackageVersion) {
        $sourceVersion = [string] $taskPortableManifest.project_version
        if ($sourceVersion -match '^\d+\.\d+\.\d+$') { $sourceVersion += '.0' }
        $PackageVersion = Get-StoreVersion $sourceVersion
    }
    Assert-ArtifactPathsAvailable $PackageVersion
    $taskName = "LiteArc-$PackageVersion-windows-x64"
    $taskPayload = Join-Path $taskWork 'payload'
    Copy-Item -LiteralPath $taskPortable -Destination $taskPayload -Recurse
    New-LogoAssets (Join-Path $taskPayload 'Assets')
    $escape = { param([string] $Value) [Security.SecurityElement]::Escape($Value) }
    $taskManifest = @"
<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
         xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
         xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
         IgnorableNamespaces="uap rescap">
  <Identity Name="$(& $escape $IdentityName)" Publisher="$(& $escape $Publisher)" Version="$PackageVersion" ProcessorArchitecture="x64" />
  <Properties>
    <DisplayName>$(& $escape $DisplayName)</DisplayName>
    <PublisherDisplayName>$(& $escape $PublisherDisplayName)</PublisherDisplayName>
    <Description>Browse, create, test and safely extract archives.</Description>
    <Logo>Assets\Logo50.png</Logo>
  </Properties>
  <Resources><Resource Language="en-US" /><Resource Language="zh-CN" /></Resources>
  <Dependencies><TargetDeviceFamily Name="Windows.Desktop" MinVersion="10.0.17763.0" MaxVersionTested="10.0.17763.0" /></Dependencies>
  <Applications>
    <Application Id="AnAnArchiver" Executable="LiteArc.exe" EntryPoint="Windows.FullTrustApplication">
      <uap:VisualElements DisplayName="$(& $escape $DisplayName)" Description="Browse, create, test and safely extract archives."
          Square150x150Logo="Assets\Logo150.png" Square44x44Logo="Assets\Logo44.png" BackgroundColor="transparent" />
    </Application>
  </Applications>
  <Capabilities><rescap:Capability Name="runFullTrust" /></Capabilities>
</Package>
"@
    $taskManifestPath = Join-Path $taskPayload 'AppxManifest.xml'
    [IO.File]::WriteAllText($taskManifestPath, $taskManifest + "`n", [Text.UTF8Encoding]::new($false))
    $taskExpectedFiles = @(Get-ChildItem -LiteralPath $taskPayload -File -Recurse | ForEach-Object {
        [pscustomobject]@{ relative_path = $_.FullName.Substring($taskPayload.Length + 1);
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    $taskTemporaryMsix = Join-Path $taskWork ($taskName + '.msix')
    Write-Host "Creating unsigned Microsoft Store package $taskName..."
    # Do not use /nv: SDK schema and semantic validation are mandatory.
    Invoke-MakeAppx @('pack', '/d', $taskPayload, '/p', $taskTemporaryMsix, '/h', 'SHA256', '/o')
    $taskUnpacked = Join-Path $taskWork 'unpacked'
    Invoke-MakeAppx @('unpack', '/p', $taskTemporaryMsix, '/d', $taskUnpacked, '/o')
    foreach ($file in $taskExpectedFiles) {
        $path = Join-Path $taskUnpacked $file.relative_path
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $file.sha256) {
            throw "MSIX round-trip verification failed: $($file.relative_path)"
        }
    }
    $taskMsix = Join-Path $taskOutput ($taskName + '.msix')
    $taskReport = [ordered]@{ schema_version = 1; created_utc = [DateTime]::UtcNow.ToString('o');
        path = $taskMsix; sha256 = (Get-FileHash -LiteralPath $taskTemporaryMsix -Algorithm SHA256).Hash.ToLowerInvariant();
        identity = [ordered]@{ name = $IdentityName; publisher = $Publisher; publisher_display_name = $PublisherDisplayName;
            display_name = $DisplayName; version = $PackageVersion; architecture = 'x64' };
        build = $taskPortableManifest.build;
        makeappx = [ordered]@{ path = $taskMakeAppx; version = (Get-Item -LiteralPath $taskMakeAppx).VersionInfo.FileVersion;
            sha256 = (Get-FileHash -LiteralPath $taskMakeAppx -Algorithm SHA256).Hash.ToLowerInvariant() };
        validation = [ordered]@{ sdk_pack_validation = $true; round_trip_sha256 = $true; payload_files = $taskExpectedFiles.Count;
            signed = $false; installation_tested = $false; store_certified = $false } }
    $taskTemporaryReport = Join-Path $taskWork ($taskName + '-artifacts.json')
    Write-Json $taskTemporaryReport $taskReport
    Assert-ArtifactPathsAvailable $PackageVersion
    $taskPublished = @()
    try {
        foreach ($artifact in @(@($taskTemporaryMsix, $taskMsix),
            @($taskManifestPath, (Join-Path $taskOutput ($taskName + '.AppxManifest.xml'))),
            @($taskTemporaryReport, (Join-Path $taskOutput ($taskName + '-artifacts.json'))))) {
            # The two-argument Move never overwrites an existing file on either .NET runtime.
            [IO.File]::Move($artifact[0], $artifact[1])
            $taskPublished += $artifact[1]
        }
    } catch {
        foreach ($path in $taskPublished) { Remove-Item -LiteralPath $path -Force }
        throw
    }
    Write-Host "Microsoft Store MSIX: $taskMsix"
    Write-Host 'Unsigned package; submit with the matching Partner Center identity. Installation and Store certification are separate checks.'
} finally {
    # Delete only this invocation's newly created direct child, never a supplied root.
    if ([IO.Path]::GetDirectoryName($taskWork) -cne $taskOutput -or [IO.Path]::GetFileName($taskWork) -notmatch '^\.msix-work-[0-9a-f]{32}$') {
        throw 'MSIX work directory escaped OutputDirectory.'
    }
    if (Test-Path -LiteralPath $taskWork) { Remove-Item -LiteralPath $taskWork -Recurse -Force }
}
