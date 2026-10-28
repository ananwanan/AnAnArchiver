# Android launcher, storage consent and small-screen layout

The Android package now uses the same blue archive box / amber zipper artwork as
the desktop application. The package template references adaptive foreground /
background resources and five bitmap density fallbacks. Refresh those committed
exports with `python scripts/export_android_icons.py` (Pillow required only for
exporting, not building).

Android 11 and later use `MANAGE_EXTERNAL_STORAGE` and the app-specific all-files
settings page, with a global settings fallback for vendor ROMs. Android 9/10 use
the read/write runtime permissions, bounded to API 29 in the manifest. Startup
explains the request; deferral or denial leaves a retry banner and menu entry.
Returning to the app refreshes the actual grant. The Java Activity and Qt
controller handle platform consent; Core and Application stay independent of Qt.

Android uses Qt's local-file dialogs and starts them at the primary shared
storage directory obtained from Android's Environment API. This applies to open,
browse, extraction destination, compression sources and save destination. Native
Android SAF pickers return `content:` URLs, which the filesystem-based Core does
not consume. Desktop file dialog defaults remain unchanged. References:
[Qt Android storage](https://www.qt.io/blog/qt-for-android-storage-updates),
[Qt FileDialog options](https://doc.qt.io/qt-6/qml-qtquick-dialogs-filedialog.html).

Mobile windows no longer use the desktop minimum width or menu bar. A compact
toolbar exposes remaining actions through a scrolling More menu. Home actions
stack, titles and notices wrap, desktop shortcut/drop instructions are hidden,
and short landscape windows can scroll. Archive navigation uses a vertical split
at narrow widths; file and folder rows use larger mobile touch targets. Compact
task status avoids forcing desktop metrics into a single row.

## Reproduction

Use matching Qt Android and host kits selected from the current dependency
resolution. The local run used Qt 6.12.0, Android NDK 28.2.13676358, SDK 36,
arm64-v8a and JDK 25. These are recorded build inputs, not version pins.

```powershell
./scripts/Build-Android.ps1 -QtAndroidDir '<android-kit>' -QtHostDir '<host-kit>' `
  -AndroidNdkDir '<ndk>' -AndroidSdkDir '<sdk>' -JavaHome '<jdk>' -JavaTcpPipes
python -m unittest tests.dependencies.test_android_storage_access `
  tests.dependencies.test_android_packaging_paths `
  tests.dependencies.test_android_superbuild_flags -v
```

The host UI run uses a fresh `out/build/android-ui-host` superbuild, the resolved
bundled archive dependencies and the matching Qt MSVC developer kit. In its
`application` directory run CTest, then run `ananarchiver_qt_desktop_flow_tests`
with `ANANARCHIVER_TEST_LANGUAGE=zh-CN` and
`ANANARCHIVER_TEST_CAPTURE_DIR=out/validation/android-mobile-fixes`. Its mobile
fixture exercises the production QML with a permission controller substitute;
it is not Android device evidence.

## Results

- Android ARM64 and Windows MSVC host builds passed; final QML lint passed with
  no diagnostics. `git diff --check` passed.
- All 18 host CTest suites have passing results. The first run found a permission
  dialog button below the 48 px touch target; after correcting it, the failed
  `qt_desktop_flow` suite passed on rerun. The final embedded Home smoke also
  passed after rebuilding the latest resources.
- The focused Android storage / packaging path / superbuild flag run passed all
  15 tests. The storage Activity also compiled against actual Android SDK 36 and
  Qt Android classes.
- Production UI flows passed in English and Chinese, covering 320, 360, 393 and
  440 logical pixel widths, 640 x 360 landscape scrolling, permission dialog and
  retry banner, six picker initial folders, scrolling menu access, and archive
  folder navigation / original-index selection after sorting duplicate names.
  Host captures are under `out/validation/android-mobile-fixes/`.
- The final APK passed debug-signature, manifest/launcher, ARM64 native dependency
  closure (81 libraries), required Qt plugin and 16 KiB ZIP/ELF alignment checks.
  Pixel comparison confirmed all six packaged launcher bitmap resources match
  the committed exports (`out/artifacts/android/icon-verification.json`).

Artifact: `out/artifacts/android/LiteArc-1.0.0-arm64-v8a-test.apk` (79,023,712 bytes).
SHA-256: `fcf926331288ef29748954d4dfa4778b6e03298b21a90efb24fc7f8d6346220b`.
The package audit is `out/artifacts/android/verification.json`; it records the
source revision and dirty working tree rather than representing these changes
as committed code.

## Validation boundary

The Java harness exercises the production Activity against small Android API
stubs, including modern grant/denial/revocation, settings fallback/failure, and
legacy runtime permission callbacks. Android SDK compilation checks the actual
Activity against Qt's Android classes. APK checks inspect the final manifest,
launcher resources, signature, ABI, required Qt plugins, native dependency
closure and 16 KiB alignment.

No Android device or emulator was connected. Xiaomi 17 rendering, installation,
launcher refresh, vendor settings interaction and real shared-storage archive
operations remain unverified on hardware. The APK is debug-signed and is a local
test package.
