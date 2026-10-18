# Packaging and platform integration

The product display names are 轻匣 (Chinese) and LiteArc (English). Public
executables and launchers use `LiteArc`, `LiteArc.exe`, `LiteArc.sh` and
`LiteArc.app`; distribution artifacts use `LiteArc-<version>-<platform>-<arch>`.
Existing registry, settings, bundle/application and desktop identities, and the
`share/AnAnArchiver` data directory, retain their compatibility names. The source
repository remains `https://github.com/ananwanan/AnAnArchiver`.

Archive and Application APIs remain independent of Qt and system registration.
Packaging scripts consume the application's CMake cache and provenance instead
of selecting a Qt installation from PATH. Windows uses the same-kit windeployqt;
Linux/macOS use Qt's CMake QML deployment API, with a same-kit macdeployqt pass
for the final macOS bundle. SVG image/icon plugins are explicitly included.

Windows packages include app-local release MSVC CRT binaries, the selected Qt DLLs,
QML imports, plugins, notices, source records and SHA-256 manifests. The installer
uses this complete portable payload. The Inno build tool is resolved from its
official stable download page; its selected version is recorded in the package.

Installation is per-user. Open With registration, Explorer verbs and a desktop
shortcut are optional. Registration never writes extension defaults or UserChoice.
Existing private shell keys must match an owned command; COM handlers and unknown
keys are preserved. Upgrade reconciles deselected integration options, and only
removes obsolete payload files whose previous ownership hash still matches.
An existing installation must be uninstalled before changing its directory.

The same installer registration code can target a restricted GUID namespace
under HKCU Software/AnAnArchiver/PackageValidation for acceptance tests. This
mode suppresses real shortcuts, automatic launch, association refresh and the
normal uninstall registration. Tests compare real user registration before and
after the complete installation/upgrade/uninstall lifecycle.

Windows payload directories can contain Unicode and exceed 260 characters. The
native uninstaller and its ownership record use a separate short per-user
installer-data directory; registry commands and shortcuts retain the actual
payload directory. An upgrade cannot silently move an existing installation.
The packaged application initializes its own DLL/plugin/QML search directories
before Qt startup and discards external Qt paths. It does not edit system
environment variables.

The Windows Qt source preparation carries a recorded patch for extended-path
library loading and dynamic module filename buffers. It also restricts the
OpenGL screen workaround to OpenGL windows, so software-rendered windows do not
probe a graphics driver unnecessarily. The application selects software Qt
Quick rendering for a long executable directory, avoiding drivers with shorter
executable-path limits. Patch hashes, upstream revisions and modified binary
hashes accompany the package. A developer-kit shadow build verifies the matching
source revision and exported ABI, leaving the original SDK and sources intact;
the normal source build applies the patch to a private source export.

Shell arguments enter the Qt application host, then the existing asynchronous
controllers and task system. Extraction starts after indexing and any metadata
query complete, with Ask overwrite and Safe links. Encrypted extraction opens
the existing password form with the requested destination retained. Compression
prefills the creation form and requires its normal confirmation. Opening another
archive, closing it or dismissing the index password form abandons a pending
shell extraction. macOS QFileOpenEvent delivers Finder/Dock file-open requests
to this same archive controller, including events queued during startup.

Linux portable packages register only a valid desktop/MIME handler, without
changing defaults. The current package directory owns its generated desktop
entry, so an older package cannot unregister a newer one. macOS bundles declare
archive document types as Viewer/Alternate handlers. Native Linux/macOS GUI,
Finder/Explorer menu interaction, publisher signing and notarization are separate
acceptance boundaries from Windows code and isolated registration checks.

References: [Qt QML deployment](https://doc.qt.io/qt-6/qt-generate-deploy-qml-app-script.html),
[Qt runtime deployment](https://doc.qt.io/qt-6/qt-deploy-runtime-dependencies.html),
[macOS deployment](https://doc.qt.io/qt-6/macos-deployment.html),
[QFileOpenEvent](https://doc.qt.io/qt-6/qfileopenevent.html),
[desktop MIME handlers](https://specifications.freedesktop.org/desktop-entry/latest/mime-types.html).
