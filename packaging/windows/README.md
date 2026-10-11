# Windows release packaging

Run from the repository after resolving current stable dependencies and building
the Release MSVC x64 frontend:

```powershell
./scripts/Package-Windows.ps1 -BuildDirectory out/build/desktop-release -BootstrapInstaller
```

The script derives the Qt kit from the application CMake cache. It deploys QML
imports and plugins with that kit's `windeployqt --force --qmldir`, deploys the
licensed MSVC redistributable CRT DLLs app-locally, compares every deployed Qt DLL
with its source, and packages source/version provenance and dependency licenses.
SVG image/icon plugins are included explicitly. Qt's prefix is relocated through
`qt.conf`, allowing the DLLs to retain their exact source-kit hashes. For official
binary kits, vendor SPDX records and extracted license notices are preserved;
missing standard license texts are obtained from their recorded Qt source
revision or SPDX's canonical license texts and cached for subsequent runs.
When the selected kit contains the project Windows long-path and platform patch, its
build record, patch and rebuild recipe are copied alongside the deployment
provenance. The packager verifies the modified DLL/import-library hashes, source
revision and compatibility result. `licenses/Modified-Qt.txt` identifies these
changed components; the original vendor SBOM remains a record of the unmodified
release. The deployed DLL hashes are checked against the selected patched kit.
Long installation paths use Qt Quick's raster software renderer. The package
does not add a separate Mesa/LLVM OpenGL runtime; raster windows avoid the GPU
selection probe through the matched Qt Windows platform patch.
For a separate dependency source checkout, pass `-DependencySourceDirectory`.
`-SkipBuild` packages an already built Release executable; `-SkipInstaller`
produces only the portable distribution. No `PATH` or Qt installation is required
on the target Windows 10/11 x64 computer. Extract the entire ZIP before launching.

Inno Setup is resolved from its official stable download page. Install the newest
stable x64 compiler manually and pass `-InstallerCompiler`, or use
`-BootstrapInstaller` to download the official signed tool into the build-tools
cache. The resolved version and source are saved in the package manifest.

The installer is per-user and needs no elevation. The optional Open with
registration, Explorer commands and desktop shortcut start unchecked. Open with
registration exposes supported formats to Windows; selecting a default remains
a Windows setting. Explorer commands appear in Windows 11's classic **Show more
options** menu. Upgrades reconcile deselected options and remove obsolete managed
files only if their original SHA-256 still matches; reparse paths and modified
files are retained. Conflicting integration commands are checked before file
installation. Uninstall removes only
commands owned by this installation and leaves user settings and user-added files.
Upgrades use the existing installation directory. Installing into another
directory while the previous installation remains registered is refused before
application files or integration values are changed. Uninstall the previous
installation first to change its location.

The installer provides a directory field with Browse and accepts the original
absolute destination, including paths beyond 260 characters. Both interactive
and silent installs validate the destination. Inno Setup 7's stock directory
validator retains a 240-character limit and its uninstall-directory code uses
normal paths, so payload paths use a validated `code:InstallRoot` callback. The
native uninstall program and log live in the private per-user
`%LOCALAPPDATA%/AnAnArchiver/Installer/<AppID>` slot. Windows' uninstall registration
points to this executable; `InstallLocation` points to the actual application
directory. The UTF-8 installation record must match its owner registration before
uninstall proceeds. The application directory can contain Chinese or emoji names.
Uninstall removes its record and its empty data slot.

## Isolated installation acceptance

`scripts/Test-Package.ps1` installs the actual release installer with
`/REGISTRYPREFIX=Software\AnAnArchiver\PackageValidation\<GUID>`. Only this
restricted namespace is accepted. It redirects the registry integration and
uninstall metadata, suppresses real shortcuts and Explorer refresh, and saves the
namespace in the installation for the uninstaller. It exercises the same
association/context-command registration and removal code without changing a
user's real default applications. The script tests no-integration installation,
upgrade with integration, upgrade with options disabled, and uninstall while
preserving a foreign extension default and a user-added file.
Test uninstall data is written below the test process's `TEMP` in the fixed
`AnAnArchiver-InstallerData-<GUID>` slot; no real profile installer data is used.
Tests locate the uninstaller through the namespace's `UninstallString`. Starting
an installed executable beyond 260 characters uses Windows' extended-length path
prefix while retaining the same physical application directory.

Create a version-changing fixture from the final portable directory:

```powershell
./scripts/New-WindowsUpgradeFixture.ps1 -PortableDirectory out/artifacts/windows/AnAnArchiver-1.0.0-windows-x64
./scripts/Test-Package.ps1 -PortableArchive out/artifacts/windows/AnAnArchiver-1.0.0-windows-x64.zip -BuildDirectory out/build/desktop-release -InstallerPath out/artifacts/windows/AnAnArchiver-0.9.0-windows-x64-upgrade-fixture-setup.exe -UpgradeInstallerPath out/artifacts/windows/AnAnArchiver-1.0.0-windows-x64-setup.exe
```

The same application payload is compiled into an installer with a lower version
and two obsolete managed-file fixtures. This verifies installer version
transitions, cleanup of unchanged obsolete files and preservation of modified
obsolete files during upgrade. It does not establish compatibility with an
unrelated historical application payload. Normal Inno uninstall removes files
tracked by its installation log, including previously managed files; user-added
files and settings are preserved.

The ZIP, setup EXE and their SHA-256 hashes are written under
`out/artifacts/windows`. `package-manifest.json` contains per-file hashes and the
Qt/runtime deployment sources. Release installers are unsigned unless signing is
added by the release owner; the build-tool download has its vendor signature.
