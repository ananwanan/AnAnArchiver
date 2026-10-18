# LiteArc Windows release packaging

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
`-SkipBuild` packages a Release build completed by the current dependency-traceable
build workflow. Its application build directory must contain both
`dependency-provenance.json` and `resolved-dependencies.json`. For a superbuild,
these records are under `application/`; for a direct application build, they are
at the build directory root. The packager checks and reads both records before
changing artifact directories, deploying runtimes or downloading installer tools.
`-SkipInstaller` produces only the portable distribution. No `PATH` or Qt
installation is required on the target Windows 10/11 x64 computer. Extract the
entire ZIP before launching.

If an older build has an EXE but lacks either record, configure and fully compile
in a new, unused directory. Reconfiguring the old directory or writing/copying
records cannot establish which dependency sources produced its existing binaries.
The following example uses the default MSVC x64 source-Qt workflow; choose another
new directory name if `out/build/desktop-release-traced` already exists:

```powershell
cmake -DANANARCHIVER_INIT_QT=ON -P scripts/init-dependencies.cmake
./scripts/Invoke-Msvc.ps1 cmake --preset desktop-release -B out/build/desktop-release-traced
./scripts/Invoke-Msvc.ps1 cmake --build out/build/desktop-release-traced --parallel 4
./scripts/Package-Windows.ps1 -BuildDirectory out/build/desktop-release-traced -SkipBuild -BootstrapInstaller
```

This rebuilds the coupled Qt sources, archive dependencies and application. The
first full source build takes considerably longer than packaging an existing build.

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
files are retained. All obsolete files are backed up and verified before any is
removed. A cleanup error or subsequent installation failure restores the removed
files. Recovery never overwrites a new, unknown file or follows a reparse path;
if recovery is blocked, setup still fails and reports the persistent backup
directory and its `restore-files.txt` record. Backups are discarded after a
successful installation. This transaction covers obsolete-file cleanup; it does
not promise rollback of every file overwritten by Inno Setup, or automatic
recovery after forced process termination or power loss. Conflicting integration commands are checked before file
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

`scripts/Test-WindowsInstallerTransaction.ps1 -InstallerCompiler <ISCC.exe>`
independently compiles small payloads and checks backup failure, a locked obsolete
file, a later installation-engine failure, blocked recovery without overwriting
unknown data, and successful cleanup with modified/reparse files in a long
Chinese/emoji directory. It uses private registry namespaces and retains its
report and Inno logs under `out/fix-main/installer-transaction`.

Create a version-changing fixture from the final portable directory:

```powershell
./scripts/New-WindowsUpgradeFixture.ps1 -PortableDirectory out/artifacts/windows/LiteArc-1.0.0-windows-x64
./scripts/Test-Package.ps1 -PortableArchive out/artifacts/windows/LiteArc-1.0.0-windows-x64.zip -BuildDirectory out/build/desktop-release -InstallerPath out/artifacts/windows/LiteArc-0.9.0-windows-x64-upgrade-fixture-setup.exe -UpgradeInstallerPath out/artifacts/windows/LiteArc-1.0.0-windows-x64-setup.exe
```

The default test constructs an installation root beyond MAX_PATH. For a separately
reported ordinary-path lifecycle, pass `-MinimumInstallPathLength 0`; this does not
replace the long-path acceptance check. The report records the requested minimum,
actual installation-root length and whether it exceeds MAX_PATH. On the validation
host, a Sogou input-method DLL crashes when the GUI executable path is too long;
see the [2026-10-18 validation record](../../docs/validation/2026-10-18-litearc.md)
for the dump evidence and the distinction between ordinary and long-path results.

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

## Microsoft Store MSIX

`scripts/Package-MSIX.ps1` creates an unsigned Windows x64 MSIX for submission to
Microsoft Store. Install the Windows SDK with `MakeAppx.exe` first. The script
discovers the newest installed numeric SDK tool version through the registered
Windows Kits root, SDK environment variables and standard installation paths.
For a specific installation, pass `-MakeAppxPath <path-to-makeappx.exe>`.

Reserve the product in Partner Center, open **Product management > Product
identity**, and copy these exact values into the required parameters:

| Parameter | Partner Center / manifest value |
| --- | --- |
| `-IdentityName` | `Package/Identity/Name` |
| `-Publisher` | `Package/Identity/Publisher` |
| `-PublisherDisplayName` | `Package/Properties/PublisherDisplayName` |

The following values are placeholders; replace all three with your product's
identity. Names, publisher strings, capitalization and punctuation must match
Partner Center. `-DisplayName` defaults to `LiteArc`; set it to the reserved
Store display name if that differs.

```powershell
./scripts/Package-MSIX.ps1 -BuildDirectory out/build/desktop-release -SkipBuild `
    -IdentityName '<Package/Identity/Name from Partner Center>' `
    -Publisher '<Package/Identity/Publisher from Partner Center>' `
    -PublisherDisplayName '<Package/Properties/PublisherDisplayName from Partner Center>'
```

Without `-SkipBuild`, the script first builds the specified Release directory.
It then calls `Package-Windows.ps1 -SkipInstaller`, preserving the same matched
Qt/QML deployment, app-local MSVC runtime, dependency records and licenses. The
existing requirements for both build provenance records also apply here. Pass
`-DependencySourceDirectory` when using a separate dependency source checkout.
This path does not need Inno Setup.

Alternatively, package the complete deployed directory previously produced by
`Package-Windows.ps1`. Its manifest and every recorded payload hash are checked
before staging; do not pass an arbitrary EXE directory or an extracted subset:

```powershell
./scripts/Package-MSIX.ps1 `
    -PortableDirectory out/artifacts/windows/LiteArc-1.0.0-windows-x64 `
    -IdentityName '<Package/Identity/Name from Partner Center>' `
    -Publisher '<Package/Identity/Publisher from Partner Center>' `
    -PublisherDisplayName '<Package/Properties/PublisherDisplayName from Partner Center>' `
    -PackageVersion 1.0.1.0
```

`-PackageVersion` defaults to the recorded project version with a fourth `.0`
component. Store versions must have four integer components: the first is
1–65535, the second and third are 0–65535, and the fourth must be zero. Set an
explicit version when the Store package needs a different build number; this
does not change the source build version recorded in provenance.

The manifest declares `Windows.Desktop`, x64, a full-trust classic desktop entry
point and the restricted `runFullTrust` capability. Its Windows version floor is
`10.0.17763.0` (Windows 10 1809), matching the current Qt 6.12 support floor.
`MaxVersionTested` conservatively uses the same value; discovering a newer SDK
does not establish that the application was tested on that Windows version.
Tile and Store logos are generated from the existing application icon without
additional image tools. This package does not add the Inno installer's optional
Open with registration, Explorer context commands or desktop shortcut.

By default, `out/artifacts/msix` receives:

- `LiteArc-<four-part-version>-windows-x64.msix`
- `LiteArc-<four-part-version>-windows-x64.AppxManifest.xml`
- `LiteArc-<four-part-version>-windows-x64-artifacts.json`, containing the
  selected identity, source provenance, packaging tool and output SHA-256 hashes.

Use `-OutputDirectory` to select another artifact directory. MakeAppx's manifest
validation remains enabled. Existing same-name artifacts are never overwritten;
use a new `-PackageVersion` or `-OutputDirectory` when packaging again. The script
unpacks the generated MSIX and checks all
staged file hashes before reporting success. It does not install the package,
modify certificate trust, run Windows App Certification Kit or submit to Store.
Those checks and the Store review remain release acceptance steps; the existing
Inno installer tests do not establish MSIX installation compatibility.

Upload the `.msix` in the product submission's **Packages** page. Store signs the
package after certification, so this submission workflow does not require a
signing certificate. Local sideload testing needs a separately signed package
whose certificate publisher matches the manifest; follow Microsoft's SignTool
instructions. Do not modify the verified submission artifact in place without
regenerating its hash record.

Official references: [manual desktop package manifest](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-manual-conversion),
[Partner Center identity values](https://learn.microsoft.com/en-us/windows/apps/publish/view-app-identity-details),
[Store package, version and signing requirements](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/app-package-requirements),
[MakeAppx and its validation limits](https://learn.microsoft.com/en-us/windows/msix/package/create-app-package-with-makeappx-tool),
[SignTool for sideload packages](https://learn.microsoft.com/en-us/windows/msix/package/sign-app-package-using-signtool),
and [Qt supported platforms](https://doc.qt.io/qt-6/supported-platforms.html).
