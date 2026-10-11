# AnAnArchiver for Linux

Extract the complete directory and run `./AnAnArchiver.sh`. The package contains
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

Run `./install-desktop.sh` to add AnAnArchiver to your current user's application
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
