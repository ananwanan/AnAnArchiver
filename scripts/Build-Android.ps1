[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $QtAndroidDir,
    [Parameter(Mandatory = $true)] [string] $QtHostDir,
    [Parameter(Mandatory = $true)] [string] $AndroidNdkDir,
    [string] $AndroidSdkDir = $env:ANDROID_HOME,
    [string] $JavaHome = $env:JAVA_HOME,
    [ValidateSet('arm64-v8a', 'armeabi-v7a', 'x86', 'x86_64')] [string] $Abi = 'arm64-v8a',
    [ValidateRange(1, 64)] [int] $Jobs = 4,
    [string] $BuildDirectory,
    [string] $ArtifactDirectory,
    [switch] $JavaTcpPipes,
    [string] $CMakeCommand = 'cmake'
)
$ErrorActionPreference = 'Stop'
$sourceDirectory = Split-Path -Parent $PSScriptRoot
if (-not $AndroidSdkDir) { $AndroidSdkDir = $env:ANDROID_SDK_ROOT }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $sourceDirectory "out/build/android-$Abi-release" }
if (-not $ArtifactDirectory) { $ArtifactDirectory = Join-Path $sourceDirectory 'out/artifacts/android' }
foreach ($requiredPath in @(
    (Join-Path $QtAndroidDir 'lib/cmake/Qt6/qt.toolchain.cmake'),
    (Join-Path $QtHostDir 'bin/androiddeployqt.exe'),
    (Join-Path $AndroidNdkDir 'build/cmake/android.toolchain.cmake'),
    (Join-Path $AndroidSdkDir 'platforms/android-36/android.jar'),
    (Join-Path $JavaHome 'bin/java.exe'))) {
    if (-not (Test-Path -LiteralPath $requiredPath)) { throw "Required toolchain file is missing: $requiredPath" }
}

function Invoke-BuildCommand([string] $Executable, [string[]] $Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE" }
}

function Read-ExactBytes([System.IO.Stream] $Stream, [int] $Count) {
    $buffer = New-Object byte[] $Count
    $read = 0
    while ($read -lt $Count) {
        $next = $Stream.Read($buffer, $read, $Count - $read)
        if ($next -eq 0) { throw 'Unexpected EOF in an APK native library' }
        $read += $next
    }
    return ,$buffer
}

$originalJavaHome = $env:JAVA_HOME
$originalQtDir = $env:QTDIR
$originalJavaToolOptions = $env:JAVA_TOOL_OPTIONS
$originalGradleOptions = $env:GRADLE_OPTS
try {
    $env:JAVA_HOME = $JavaHome
    $env:QTDIR = $QtAndroidDir
    # Let every Java process finish with the build, including in captured terminals.
    $env:GRADLE_OPTS = "$originalGradleOptions -Dorg.gradle.daemon=false".Trim()
    if ($JavaTcpPipes) {
        # A failing Windows AF_UNIX implementation can prevent Gradle's Java
        # selector pipes from starting. An unavailable Unix socket directory
        # makes the JDK use its TCP fallback, only in this build's processes.
        $unavailableSocketDirectory = [System.IO.Path]::GetFullPath((Join-Path $BuildDirectory 'disabled-unix-domain-sockets'))
        if (Test-Path -LiteralPath $unavailableSocketDirectory) { throw 'The TCP fallback socket directory must not exist' }
        $env:JAVA_TOOL_OPTIONS = "$originalJavaToolOptions -Djava.net.preferIPv4Stack=true -Djdk.net.unixdomain.tmpdir=`"$unavailableSocketDirectory`"".Trim()
    }
    # Release native code, with the standard debug signing key for local installation.
    # Publishing requires the owner's release key and a separate release package.
    Invoke-BuildCommand $CMakeCommand @(
        '-S', $sourceDirectory, '-B', $BuildDirectory, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF',
        '-DANANARCHIVER_BUILD_QT=ON', '-DANANARCHIVER_USE_SYSTEM_QT=ON',
        "-DCMAKE_TOOLCHAIN_FILE=$QtAndroidDir/lib/cmake/Qt6/qt.toolchain.cmake",
        "-DCMAKE_PREFIX_PATH=$QtAndroidDir", "-DQT_HOST_PATH=$QtHostDir",
        "-DANDROID_SDK_ROOT=$AndroidSdkDir", "-DANDROID_NDK_ROOT=$AndroidNdkDir",
        "-DANDROID_ABI=$Abi", '-DANDROID_PLATFORM=android-28',
        '-DQT_ANDROID_DEPLOYMENT_TYPE=DEBUG', "-DANANARCHIVER_DEPENDENCY_JOBS=$Jobs")
    Invoke-BuildCommand $CMakeCommand @('--build', $BuildDirectory, '--parallel', '2')
    $applicationBuild = Join-Path $BuildDirectory 'application'
    Invoke-BuildCommand $CMakeCommand @('--build', $applicationBuild, '--target', 'apk', '--parallel', "$Jobs")

    $builtApk = Get-ChildItem -LiteralPath (Join-Path $applicationBuild 'frontend/qt') -Recurse -File -Filter '*-debug.apk' |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $builtApk) { throw 'The Android build did not produce a signed debug APK' }
    New-Item -ItemType Directory -Path $ArtifactDirectory -Force | Out-Null
    $artifactApk = $builtApk.FullName
    $finalApk = Join-Path $ArtifactDirectory "LiteArc-1.0.0-$Abi-test.apk"
    $buildTools = Get-ChildItem -LiteralPath (Join-Path $AndroidSdkDir 'build-tools') -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $buildTools) { throw 'Android SDK build-tools are missing' }
    Invoke-BuildCommand (Join-Path $buildTools.FullName 'apksigner.bat') @('verify', '--verbose', '--print-certs', $artifactApk)
    Invoke-BuildCommand (Join-Path $buildTools.FullName 'zipalign.exe') @('-c', '-P', '16', '4', $artifactApk)
    $badging = & (Join-Path $buildTools.FullName 'aapt.exe') dump badging $artifactApk
    if ($LASTEXITCODE -ne 0) { throw 'APK manifest inspection failed' }
    if (-not ($badging -match "package: name='com.ananwanan.ananarchiver'")) { throw 'Unexpected APK package name' }
    if (-not ($badging -match "sdkVersion:'28'")) { throw 'Unexpected minimum Android API' }
    if (-not ($badging -match "targetSdkVersion:'36'")) { throw 'Unexpected target Android API' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $apkArchive = [System.IO.Compression.ZipFile]::OpenRead($artifactApk)
    try {
        $nativeFiles = @($apkArchive.Entries | Where-Object { $_.FullName -match '^lib/[^/]+/[^/]+\.so$' } | ForEach-Object FullName)
        $actualAbis = @($nativeFiles | ForEach-Object { ($_ -split '/')[1] } | Sort-Object -Unique)
        if ($actualAbis.Count -ne 1 -or $actualAbis[0] -ne $Abi) { throw "Unexpected APK ABI: $actualAbis" }
        if (-not ($nativeFiles -match '/libLiteArc_') -or -not ($nativeFiles -match '/libc\+\+_shared.so$')) {
            throw 'Required application or C++ runtime library is missing from the APK'
        }
        $requiredPlugins = @(
            'libplugins_platforms_qtforandroid', 'libplugins_imageformats_qsvg',
            'libqml_QtQml_qmlplugin', 'libqml_QtQml_Models_modelsplugin',
            'libqml_QtQuick_qtquick2plugin', 'libqml_QtQuick_Window_quickwindowplugin',
            'libqml_QtQuick_Controls_qtquickcontrols2plugin',
            'libqml_QtQuick_Controls_Basic_qtquickcontrols2basicstyleplugin',
            'libqml_QtQuick_Controls_Basic_impl_qtquickcontrols2basicstyleimplplugin',
            'libqml_QtQuick_Templates_qtquicktemplates2plugin',
            'libqml_QtQuick_Layouts_qquicklayoutsplugin',
            'libqml_QtQuick_Dialogs_qtquickdialogsplugin',
            'libqml_QtQuick_Dialogs_quickimpl_qtquickdialogs2quickimplplugin',
            'libqml_Qt_labs_folderlistmodel_qmlfolderlistmodelplugin')
        foreach ($plugin in $requiredPlugins) {
            if ($nativeFiles -notcontains "lib/$Abi/${plugin}_$Abi.so") { throw "Required Qt plugin is missing: $plugin" }
        }
        $auditDirectory = Join-Path $BuildDirectory 'apk-native-audit'
        New-Item -ItemType Directory -Path $auditDirectory -Force | Out-Null
        $auditLibraries = @()
        $expectedMachine = @{ 'arm64-v8a' = 183; 'armeabi-v7a' = 40; 'x86' = 3; 'x86_64' = 62 }[$Abi]
        foreach ($nativeFile in $nativeFiles) {
            $libraryStream = $apkArchive.GetEntry($nativeFile).Open()
            try {
                $ident = Read-ExactBytes $libraryStream 16
                if ($ident[0] -ne 127 -or $ident[1] -ne 69 -or $ident[2] -ne 76 -or $ident[3] -ne 70 -or $ident[5] -ne 1) {
                    throw "Invalid little-endian ELF library: $nativeFile"
                }
                $is64 = $ident[4] -eq 2
                if ($ident[4] -notin @(1, 2)) { throw "Invalid ELF class: $nativeFile" }
                $headerSize = if ($is64) { 64 } else { 52 }
                $header = [byte[]]($ident + (Read-ExactBytes $libraryStream ($headerSize - 16)))
                if ([BitConverter]::ToUInt16($header, 18) -ne $expectedMachine) { throw "Unexpected ELF architecture: $nativeFile" }
                $programOffset = if ($is64) { [BitConverter]::ToUInt64($header, 32) } else { [BitConverter]::ToUInt32($header, 28) }
                $programSize = [BitConverter]::ToUInt16($header, $(if ($is64) { 54 } else { 42 }))
                $programCount = [BitConverter]::ToUInt16($header, $(if ($is64) { 56 } else { 44 }))
                if ($programOffset -lt $headerSize -or $programOffset -gt 1048576 -or $programSize -gt 256 -or $programCount -gt 256) {
                    throw "Invalid ELF program header bounds: $nativeFile"
                }
                $null = Read-ExactBytes $libraryStream ([int]$programOffset - $headerSize)
                for ($segment = 0; $segment -lt $programCount; $segment++) {
                    $programHeader = Read-ExactBytes $libraryStream $programSize
                    if ([BitConverter]::ToUInt32($programHeader, 0) -eq 1) {
                        $alignment = if ($is64) { [BitConverter]::ToUInt64($programHeader, 48) } else { [BitConverter]::ToUInt32($programHeader, 28) }
                        if ($alignment -lt 16384) { throw "ELF LOAD segment does not support 16 KiB pages: $nativeFile" }
                    }
                }
            } finally { $libraryStream.Dispose() }
            $auditLibrary = Join-Path $auditDirectory ([System.IO.Path]::GetFileName($nativeFile))
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($apkArchive.GetEntry($nativeFile), $auditLibrary, $true)
            $auditLibraries += [System.IO.Path]::GetFullPath($auditLibrary)
        }
    } finally { $apkArchive.Dispose() }
    $ndkToolchain = Get-ChildItem -LiteralPath (Join-Path $AndroidNdkDir 'toolchains/llvm/prebuilt') -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin/llvm-readelf.exe') } | Select-Object -First 1
    if (-not $ndkToolchain) { throw 'The NDK llvm-readelf tool is missing' }
    $dynamicSections = & (Join-Path $ndkToolchain.FullName 'bin/llvm-readelf.exe') --dynamic @auditLibraries
    if ($LASTEXITCODE -ne 0) { throw 'ELF dynamic dependency inspection failed' }
    $neededLibraries = @([regex]::Matches(($dynamicSections -join "`n"), 'Shared library: \[([^\]]+)\]') |
        ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    $triple = @{ 'arm64-v8a' = 'aarch64-linux-android'; 'armeabi-v7a' = 'arm-linux-androideabi'; 'x86' = 'i686-linux-android'; 'x86_64' = 'x86_64-linux-android' }[$Abi]
    $systemLibraries = @(Get-ChildItem -LiteralPath (Join-Path $ndkToolchain.FullName "sysroot/usr/lib/$triple/28") -Filter '*.so' | ForEach-Object Name)
    $packagedLibraries = @($nativeFiles | ForEach-Object { [System.IO.Path]::GetFileName($_) })
    foreach ($neededLibrary in $neededLibraries) {
        if ($neededLibrary -notin $packagedLibraries -and $neededLibrary -notin $systemLibraries) {
            throw "A native DT_NEEDED dependency is neither packaged nor public in Android API 28: $neededLibrary"
        }
    }
    $sourceRevision = & git -C $sourceDirectory rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Source revision inspection failed' }
    $archiveRevision = & git -C (Join-Path $sourceDirectory 'third_party/libarchive') rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Backend revision inspection failed' }
    # Publish the artifact only after every inspection succeeds.
    Copy-Item -LiteralPath $builtApk.FullName -Destination $finalApk -Force
    $badging | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'apk-badging.txt') -Encoding utf8
    $dynamicSections | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'elf-dynamic-dependencies.txt') -Encoding utf8
    [ordered]@{
        apk = [System.IO.Path]::GetFullPath($finalApk)
        sha256 = (Get-FileHash -LiteralPath $finalApk -Algorithm SHA256).Hash.ToLowerInvariant()
        sourceRevision = $sourceRevision
        libarchiveRevision = $archiveRevision
        abi = $Abi
        minSdk = 28
        targetSdk = 36
        nativeConfiguration = 'Release'
        packageSigning = 'Android debug key; local testing only'
        nativeLibraryCount = $nativeFiles.Count
        nativeElfArchitecture = 'Validated for every packaged native library'
        nativeElfLoadAlignment = '16 KiB minimum'
        qtRequiredPlugins = 'Platform, QML, Quick, Basic/impl, Templates, Layouts, Dialogs/quickimpl, folderlistmodel, SVG'
        nativeDependencyClosure = 'All DT_NEEDED libraries packaged or public in Android API 28'
        deviceValidation = 'Not performed by this build script'
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'verification.json') -Encoding utf8
    Write-Output "APK: $finalApk"
} finally {
    $env:JAVA_HOME = $originalJavaHome
    $env:QTDIR = $originalQtDir
    $env:JAVA_TOOL_OPTIONS = $originalJavaToolOptions
    $env:GRADLE_OPTS = $originalGradleOptions
}
