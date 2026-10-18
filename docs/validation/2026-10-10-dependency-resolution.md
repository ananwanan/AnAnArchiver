# Dependency resolution and desktop validation — 2026-10-10

> Historical validation: this run predates the 2026-10-18 rename to 轻匣 / LiteArc.
> Original AnAnArchiver artifact names, build paths and results are preserved as evidence.

Historical source note (2026-10-17): the libarchive source and backend patch
described here belong to the former fork. Current dependency initialization uses
the official [libarchive/libarchive](https://github.com/libarchive/libarchive)
latest stable release. The old revisions, patch details, and results below are
preserved as historical evidence and do not validate the current official source.

Implemented latest-stable source resolution, synchronized CI, and verified the
updated Core and Qt desktop frontend. Resolution started on October 9; final
validation completed on October 10, Asia/Shanghai.

## Source selection and reproduction

`scripts/resolve-dependencies.py` uses Python's standard library and Git. It
selects non-draft, non-prerelease official Releases, or recognized stable tags
where a designated upstream does not publish Releases. The then-designated libarchive
fork had no independent release; its latest default-branch commit was selected,
including its actual `3.9.0dev` source version. Inherited fork tags do not replace
that branch selection. Metadata errors fail explicitly; there is no old-version
fallback. `GITHUB_TOKEN`/`GH_TOKEN` supports authenticated public metadata access
without writing credentials into the record.

The source manifest records resolution time, URLs, versions, refs and exact
commits. Qt modules follow the selected supermodule gitlinks. Mbed TLS framework,
TF-PSA-Crypto and its nested modules follow the selected Mbed TLS release.
Initialization does not fetch the parent's historical gitlink first, so an
unavailable old commit cannot prevent resolving the new source set.

The resolved set used in this validation includes:

| Source | Version | Revision |
| --- | --- | --- |
| Former fork: ananwanan/libarchive | 3.9.0dev, default branch | `3bdd98d298af3ba33fb10c51582f05ddd0584c35` |
| zlib | 1.3.2 | `da607da739fa6047df13e66a2af6b8bec7c2a498` |
| bzip2 | 1.0.8 | `6a8690fc8d26c815e798c588f796eabe9d684cf0` |
| XZ | 5.8.4 | `d3e650e63c110e830fd5391e7f8b45df0b91d3da` |
| Zstandard | 1.5.7 | `f8745da6ff1ad1e7bab384bd1f9d742439278e99` |
| LZ4 | 1.10.0 | `ebb370ca83af193212df4dcbadcc5d87bc0de2f0` |
| Expat | 2.9.0 | `a76b1174c1ea52abb639a4c69d52b630a8a106a2` |
| Mbed TLS | 4.2.0 | `ece41aa84d7879d7e55c59e955a5884b541f7f3b` |
| TF-PSA-Crypto, coupled | 1.2.0 | `73c5da561c8e5253db7b1fb440eda86fde8d8024` |
| Qt | 6.12.0 | `ef25002ed292b132d1ed3246bfcee1d24a9cd966` |
| Vulkan-Headers | 1.4.365 | `c46850864f4661461b0f6cb9922c058ffea4915e` |

These are observations from the historical resolution, not permanent pins. A new
normal initialization resolves the currently configured official upstreams again.

```sh
# Normal initialization selects current stable sources.
cmake -P scripts/init-dependencies.cmake

# Resolve once, then replay the same complete source set.
python scripts/resolve-dependencies.py --qt --resolve-only --manifest out/dependencies/selected.json
python scripts/resolve-dependencies.py --qt --replay-manifest out/dependencies/selected.json

# Offline reproduction requires the recorded Git objects to exist locally.
python scripts/resolve-dependencies.py --qt --offline --replay-manifest out/dependencies/selected.json

# Core/system-Qt jobs omit Qt checkouts while preserving the complete record.
python scripts/resolve-dependencies.py --core-only --replay-manifest out/dependencies/selected.json
```

All selected checkouts and existing nested sources are checked for tracked changes
and untracked files before checkout mutation. Reinitializing an empty submodule
materializes its files even when Git retained the same HEAD/index. No forced reset
or forced checkout discards existing work. Manifest writes use a flushed temporary
file followed by atomic replacement.

CMake rejects missing/mismatched/dirty source resolutions and incompatible
installed dependency records. An explicitly selected Qt kit must match the
resolved Qt release when the source manifest includes Qt. Every application build
writes `resolved-dependencies.json` and `dependency-provenance.json`; installation
preserves these under `share/AnAnArchiver`. Source-only Core records remain valid
replay manifests. Backend patch changes use a separate generated source/build
directory, avoiding cached CMake source-directory mismatches.

## Required backend compatibility

Latest Mbed TLS removed the legacy AES, PBKDF2, HMAC and digest interfaces used by
the designated libarchive branch. The backend fix in
`cmake/patches/libarchive-mbedtls4.patch` implements the public PSA API, preserves
Mbed TLS 3 support, serializes shared PSA initialization/calls, retains crypto
failure results, and releases task-local keys/operations. AES reuses one multipart
ECB operation while preserving ZIP's little-endian counter semantics. Windows
digest probes include the required `bcrypt` link dependency.

The fix also carries the existing ZIP compressor abort-cleanup regression fix,
which is absent from the latest default branch. It is applied to a private
generated copy of the actual backend source, leaving all resolved upstream
checkouts clean. Parser/crypto implementation remains in libarchive.

- Backend fix local commit: `323e51dbd222356dc3a55102c57f60170ee19f82`.
- Patch SHA-256: `12f9ca018336556d56b97e70bfff71fbd8c469c504eb5404634d44573a3574af`.
- `build-patches.json` records that commit, hash and upstream base revision.
- Application provenance records both the upstream revision and applied patch.

The backend commit has not been pushed upstream. Once upstream incorporates it,
the patch should be removed or reconciled explicitly; patch-application failure
does not select an older backend silently.

Mbed TLS/TF Git sources require their generators. The superbuild enables
`GEN_FILES`, uses build-directory output, discovers Perl (including Git for
Windows' installation), and checks Python `jinja2`/`jsonschema` before building.
Qt's image-codec feature switches were synchronized with the current source
feature names. Qt frontend changes remove a TreeView delegate ID shadow and use
the floating-point drag-event constructor with a guard for older supported kits.

## Validation evidence

Windows tools: MSVC 19.51.36260.0, CMake 4.4.0, Ninja 1.13.2, Python 3.14.7,
Perl 5.42.3. Generated test data used `D:/Cache/Codex/AnAnArchiver-Dependencies`.

| Check | Result |
| --- | --- |
| Resolver offline suite | 23/23, including stable/prerelease filtering, fork policy, coupled sources, annotated tags, metadata failure, exact replay, malformed records, dirty/nested sources, missing objects, submodule reinitialization and atomic record writing |
| Actual complete source resolution and offline replay | Passed; final replay preserved all selected revisions |
| Core-only MSVC superbuild, Windows CNG disabled | Passed; PSA crypto and all six Mbed digest implementations detected |
| Final Core/Application regression | 5/5: core, archive, extraction, compression, application |
| Qt 6.12.0 desktop-developer build | Passed using the official matching MSVC kit |
| Final desktop regression | 14/14: five Core/Application groups plus nine Qt groups |
| Final QML ID adjustment | Five affected production-QML suites passed at Chinese, 200% scale |
| Production QML lint | Passed without warnings after Qt compatibility adjustments |
| English/Chinese production flow, 100/125/150/200% | All eight combinations passed |
| Native Windows executable smoke | Exit 0, including the final rebuilt QML |
| Full bundled Qt source superbuild configuration | Passed with matching modules; system image codecs disabled |
| Source-revision mismatch / stale installed-prefix negative checks | Both rejected before the respective compilation stage |
| Workflow YAML, actionlint 1.7.12, presets, diff whitespace | Passed |

The resolver suite was also exercised through CTest (initial integrated totals
6/6 Core and 15/15 desktop). After backend-only changes it was not repeated;
the final backend-linked groups were rerun separately. Final JUnit records are
`out/build/{core-release,desktop-developer}/application/ctest-final.xml`.

Core reproduction for the additional Windows PSA path:

```powershell
./scripts/Invoke-Msvc.ps1 cmake --preset core-release -DANANARCHIVER_LIBARCHIVE_USE_CNG=OFF
./scripts/Invoke-Msvc.ps1 cmake --build --preset core-release
./scripts/Test-Desktop.ps1 -BuildDirectory out/build/core-release/application -TemporaryRoot D:/Cache/Codex/AnAnArchiver-Dependencies
```

The normal Windows desktop uses CNG. Set `QTDIR` to the matching Qt release, then
configure/build `desktop-developer` and run `Test-Desktop.ps1` against its
`application` subdirectory. Its saved provenance confirms Qt `6.12.0`, the
upstream backend revision, and the final compatibility-patch hash.

Independent backend validation used Linux/WSL GCC 13.3, Mbed TLS 4.2.0 and
TF-PSA-Crypto 1.2.0 with OpenSSL/CNG/Nettle disabled: 13 selected backend tests,
18,729 assertions, zero failures. These included RFC PBKDF2/HMAC known answers,
failure handling, six digest algorithms, external AES ZIP fixtures, read/write
and ZIP abort cleanup. Eight workers completed 4,000 full crypto lifecycles from
cold initialization without failure. The retained Mbed TLS 3.6.7 branch also
compiled and passed that concurrency check. A native MSVC probe compiled all six
PSA digest branches and passed the SHA-256 known answer.

## CI and boundaries

CI resolves one source set and uploads it for every compiler/sanitizer/desktop
job. Core/sanitizer jobs replay Core only. The Windows desktop job builds Qt from
the resolved source modules, then runs QML lint, production flows, archive/drag
tests, native executable offscreen smoke and captures provenance/JUnit artifacts.
Current stable build tools/generator packages and compatible runner compilers are
selected rather than fixed historical dependency releases.

The workflow has been edited and locally linted; no remote Actions run was
triggered. Local desktop tests used the official Qt binary kit; the full Qt source
tree was configured but not compiled in this run. The independent WSL backend
fixture lacked LZMA/XZ/ZSTD, so those optional backend abort subcases were skipped;
the Windows Core compression regressions exercised the complete bundled codecs.
One optional Windows case-sensitive directory fixture was unavailable (error 5).
macOS/Linux GUI, full compiler/sanitizer CI matrix, Android/device execution and
actual Explorer/Finder reception of outgoing drags remain unverified.
