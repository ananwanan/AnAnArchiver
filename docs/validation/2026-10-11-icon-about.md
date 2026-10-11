# Application icon and About information

The original transparent application icon is stored in
`frontend/qt/resources/icons/ananarchiver.png`; its generation prompt and platform
exports are documented alongside it. Windows embeds seven ICO frames in the EXE;
Qt uses the PNG for the window icon and About dialog; macOS packages the ICNS.

About now displays the build timestamp in local time with an explicit UTC offset,
and a keyboard-accessible GitHub link. The UTC timestamp is generated during the
frontend build, including incremental builds. `SOURCE_DATE_EPOCH` is honored.

## Windows verification

- Release MSVC build using the existing bundled Qt dependency installation passed.
- All 14 CTest suites passed with the SSD temporary directory.
- QML lint and `git diff --check` passed.
- Native Windows About captures passed in English and Chinese with requested
  `QT_SCALE_FACTOR` values 1, 1.25, 1.5 and 2. The host monitor also applies its
  native DPI. Inspected captures showed the icon, timestamp and complete GitHub
  URL without clipping. Captures are in `out/icon-assets/about-*-windows.png`.
- Native EXE resource inspection found icon group 101 and seven 32-bit PNG-backed
  frames at 16, 24, 32, 48, 64, 128 and 256 pixels. The manifest still includes
  long-path awareness, PerMonitorV2 and `asInvoker` execution privileges.
- ICO and ICNS container sizes and all embedded PNG dimensions were verified.
- A fixed `SOURCE_DATE_EPOCH=1700000000` produced `2023-11-14T22:13:20Z`;
  repeating it did not rewrite the generated header. Clearing it restored the
  current UTC time.

The initially reused build directory contained a stale test object with an old
inline destructor. Cleaning the application build and rebuilding resolved its
duplicate-symbol link failure without changing unrelated source code.

Reproduce the build and checks:

```powershell
./scripts/Invoke-Msvc.ps1 cmake --build out/build/desktop-release/application --parallel 4
./scripts/Test-Desktop.ps1 -BuildDirectory out/build/desktop-release/application -TemporaryRoot D:/Cache/Codex/AnAnArchiver-IconAbout -Jobs 4
./scripts/Invoke-Msvc.ps1 cmake --build out/build/desktop-release/application --target AnAnArchiver_qmllint
./scripts/Invoke-Msvc.ps1 cmake --build out/build/desktop-release/application --target AnAnArchiver --parallel 4
./scripts/Start-Desktop.ps1 out/build/desktop-release/application --language zh-CN --smoke-about --smoke-test 1200 --smoke-screenshot out/icon-assets/about.png
```

macOS/Linux runtime and packaging were not exercised. Android launcher packaging
was outside this desktop change. The GitHub link's visible URL and QML activation
handler were checked; external browser activation was not exercised.
