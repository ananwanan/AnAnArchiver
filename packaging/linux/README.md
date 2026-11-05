# LiteArc for Linux

> Scope update (2026-11-05): `main` targets Flutter. Commands, deployment
> details and test results in this guide describe the existing Qt baseline,
> preserved in `codex/qt-baseline-2026-11-05` at
> `53185742ad1e640b24da397cfa3544507af4edef`. Flutter packaging is not implemented.
> See [the Flutter decision](../../docs/architecture/0007-flutter-frontend.md)
> and [Qt baseline build instructions](../../docs/development/qt-baseline.md).

## Flutter packaging requirements (pending)

Use the selected stable Flutter SDK/engine, Dart AOT output/assets, the C ABI,
backend/codecs and required native plugins; exclude Qt from the Flutter runtime.
Preserve actual build provenance, dependency licenses and payload hashes.
Adapt and verify the packaging scripts before documenting Flutter commands as
available. Existing `Package-*` scripts still operate on Qt builds.

Keep optional per-user desktop/MIME registration and preserve defaults.
Validate the actual Flutter/native plugin runtime dependencies, minimum glibc
baseline, X11/Wayland behavior and system file dragging on intended distributions.
The Qt portable bundle and CMake deployment rules below do not package Flutter.

## Existing Qt package instructions

Extract the complete directory and run `./LiteArc.sh`. The package contains
Qt, QML imports and plugins from the selected build kit. The host still supplies
glibc, its graphics drivers, X11/Wayland and other normal system libraries; build
on the oldest distribution you intend to support.

To build a package, use `scripts/Package-Linux.sh BUILD_DIRECTORY
[OUTPUT_DIRECTORY]` with a configured Release desktop build (the script also
accepts a completed superbuild directory). It preserves the cache's selected Qt
kit and records its version and dependency revisions. Source builds include Qt's
`third_party/qt/LICENSES`; a developer kit may specify the matching directory with
`ANANARCHIVER_QT_LICENSE_DIRECTORY`.
Backend and codec notices are collected from the actual manifest's source
revisions. If those trees live elsewhere, set
`ANANARCHIVER_DEPENDENCY_SOURCE_DIRECTORY` to their repository root. Complete
notices, source links and license hashes are in `share/licenses`.

Run `./install-desktop.sh` to add LiteArc to your current user's application
menu and Open With choices. Archive associations use standard MIME types and
preserve your existing default applications. Choose a default explicitly in your
file manager if desired.

Keep the extracted directory in its final location before registering it. After
moving or upgrading to a different directory, run the new copy's registration
script. An old copy's `uninstall-desktop.sh` will not remove the new registration.
Run `./uninstall-desktop.sh` before deleting the current package directory.

System packaging may instead install the executable, desktop file and icon with
`cmake --install`; distribution package managers should refresh their desktop
database in the usual post-install/post-remove hooks. No default association is
changed by CMake installation.
