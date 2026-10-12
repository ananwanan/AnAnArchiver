# AnAnArchiver for macOS

Unzip the package and move `AnAnArchiver.app` into Applications, or drag it to the
Applications shortcut in the disk image. The complete bundle contains the
selected Qt kit's frameworks, QML imports, plugins and application icon.

Finder's Open With menu recognizes the supported archive types. Use Finder's
Get Info → Open with → Change All to make AnAnArchiver a default for a selected
type. Installing the app registers it as an alternate viewer and preserves
existing defaults. Double-click and document-open events use the same async
archive controller as the application's Open command.

To upgrade, quit the application and replace its bundle in Applications. To
uninstall, quit and move the bundle to Trash; user settings and recent-archive
history are preserved. No separate privileged installer or background service
is installed.

`scripts/Package-macOS.sh BUILD_DIRECTORY [OUTPUT_DIRECTORY] [--dmg]` creates a
ZIP and, optionally, a drag-to-Applications DMG. The default bundle uses Qt's
ad-hoc signature for local testing. `--sign "Developer ID Application: …"` uses
the selected identity and hardened runtime options. Developer ID certificates,
Apple notarization submission and stapling remain release-owner tasks; the
script does not claim that an ad-hoc package is notarized.

Pass a configured Release desktop build, or its completed superbuild directory.
The script includes Qt's `third_party/qt/LICENSES`; a developer kit may specify
its matching license directory with `ANANARCHIVER_QT_LICENSE_DIRECTORY`.
Backend and codec notices follow the actual manifest's source revisions; set
`ANANARCHIVER_DEPENDENCY_SOURCE_DIRECTORY` if those trees live elsewhere. The
bundle's `Contents/Resources/licenses` contains notices, source links and hashes.

The package script validates bundle metadata, code signatures, relocatable
Mach-O dependencies and an application startup smoke test on the macOS build
host. Finder integration and clean-machine deployment still need macOS
acceptance testing on the intended x86_64/arm64 targets.
