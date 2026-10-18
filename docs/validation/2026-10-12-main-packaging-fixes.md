# Main packaging fixes — 2026-10-12

> Historical validation: this run predates the 2026-10-18 rename to 轻匣 / LiteArc.
> Original AnAnArchiver artifact names, build paths and results are preserved as evidence.

Fixed the three requested packaging regressions on top of main `d947b28`.
Implementation commit: `889c9cdf49f2e5800895f6ec4d176980eb08e73d`.

- Windows package acceptance now requires the current `dependency_tools` suite.
  The shared coverage guard is exercised against the real configured CTest list,
  and still rejects a list from which dependency_tools was removed. This check
  is registered in Windows desktop CTest when PowerShell 7 is available.
- Linux and macOS package scripts build `ananarchiver_build_info`, which waits
  for the desktop executable and then refreshes final provenance. A real
  CMake/Ninja fixture runs both production package scripts up to their install
  boundary, uses the production provenance writer, and verifies first-time
  generation and replacement of a stale application Git revision.
- Windows obsolete-file cleanup now stages and verifies all recovery copies
  before deleting anything. Cleanup and subsequent installation errors restore
  removed files. Unsafe paths or unknown replacement files are not overwritten;
  incomplete recovery remains a failure and retains persistent copies plus a
  UTF-8 restore record. Backups are discarded after installation completes.

## Validation

The Release MSVC backend/codecs and desktop were rebuilt in a new directory,
`out/build/main-packaging-fixes`, against the verified source manifest and the
matching patched Qt 6.12 kit. The application build provenance records the
implementation commit with `dirty=false`; no old dependency prefix was relabelled.

- Complete Core/Application/production Qt CTest: **18/18 passed**, including the
  52 dependency-tool unit tests, package coverage regression and six platform
  metadata/packaging tests. Total CTest time: 89.50 seconds.
- Production QML lint passed. Git diff checks, PowerShell parsing and workflow
  YAML parsing passed.
- The new Windows ZIP/setup and previous-version fixture were built in
  `out/fix-main/artifacts`, preserving the earlier release artifacts.
- Full package acceptance: **19/19 checks passed**, covering Qt/CRT hashes,
  native English/Chinese startup, deliberately contaminated Qt environment,
  Unicode/long-path shell actions, installation, foreign handler/relocation
  refusal, version upgrade, integration enable/disable and uninstall cleanup.
  Real user associations/UserChoice stayed unchanged; test files and namespace
  were cleaned. Report: `out/fix-main/artifacts/package-validation/verification.json`.
- Actual Inno 7.1 transaction regression: **5/5 passed**. Backup failure and a
  locked second obsolete file leave the original payload/registration intact;
  a subsequent real installation-engine error restores removed obsolete files;
  an unknown replacement file is preserved while recovery data and an explicit
  error are retained; successful long-path upgrade preserves user-modified and
  reparse entries and discards backups. Report:
  `out/fix-main/installer-transaction/transaction-results.json`.
  The same regression is now invoked by the Windows desktop CI job.
- Restoring the original application-only target in isolated copies of both
  Unix scripts made the fresh-provenance regression fail, confirming the new
  test detects the original defect.

## Reproduction

```powershell
./scripts/Package-Windows.ps1 -BuildDirectory out/build/main-packaging-fixes -OutputDirectory out/fix-main/artifacts -SkipBuild -InstallerCompiler <ISCC.exe>
./scripts/New-WindowsUpgradeFixture.ps1 -PortableDirectory out/fix-main/artifacts/AnAnArchiver-1.0.0-windows-x64
./scripts/Test-Package.ps1 -PortableArchive out/fix-main/artifacts/AnAnArchiver-1.0.0-windows-x64.zip -BuildDirectory out/build/main-packaging-fixes -InstallerPath out/fix-main/artifacts/AnAnArchiver-0.9.0-windows-x64-upgrade-fixture-setup.exe -UpgradeInstallerPath out/fix-main/artifacts/AnAnArchiver-1.0.0-windows-x64-setup.exe -TemporaryRoot <SSD test directory>
./scripts/Test-WindowsInstallerTransaction.ps1 -InstallerCompiler <ISCC.exe> -TemporaryRoot <SSD test directory>
```

The build/output directories above describe this run. Both regression scripts
accept other matching build/compiler/temp paths.

## Boundaries

The rollback transaction protects obsolete-file cleanup; it does not make every
Inno payload overwrite atomic or automatically recover after forced process
termination/power loss. If another file blocks recovery, the original backup is
retained and the setup fails visibly. Recovery instructions must be reviewed
before manual restoration.

Unix regression checks exercise the real build/record/package boundary through
an isolated fixture on Windows; they do not claim native Linux/macOS GUI,
packaging, MIME/Finder or signing acceptance. The installer remains unsigned.
Remote CI was not rerun during this task. The other four findings in the prior
main audit were outside the user's three-item correction scope.
