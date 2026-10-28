# Dependency update — 2026-10-28

This records one latest-stable resolution, not a permanent version constraint.
Resolution time: `2026-10-28T15:46:45.521277+00:00`.

Vulkan-Headers advances from `v1.4.365`
(`c46850864f4661461b0f6cb9922c058ffea4915e`) to
[`v1.4.366`](https://github.com/KhronosGroup/Vulkan-Headers/tree/v1.4.366)
(`73814d006638aa00bd553215b054f0f02e457e22`). Other direct dependencies
already match the latest stable upstream releases or stable tags.

| Dependency | Resolved version | Revision |
| --- | --- | --- |
| libarchive | 3.8.10 | `0c48412be2d94afe1859a807ea7896a18d8900bd` |
| zlib | 1.3.2 | `da607da739fa6047df13e66a2af6b8bec7c2a498` |
| bzip2 | 1.0.8 | `6a8690fc8d26c815e798c588f796eabe9d684cf0` |
| XZ | 5.8.4 | `d3e650e63c110e830fd5391e7f8b45df0b91d3da` |
| Zstandard | 1.5.7 | `f8745da6ff1ad1e7bab384bd1f9d742439278e99` |
| LZ4 | 1.10.0 | `ebb370ca83af193212df4dcbadcc5d87bc0de2f0` |
| Expat | 2.9.0 | `a76b1174c1ea52abb639a4c69d52b630a8a106a2` |
| Mbed TLS | 4.2.0 | `ece41aa84d7879d7e55c59e955a5884b541f7f3b` |
| Qt | 6.12.0 | `ef25002ed292b132d1ed3246bfcee1d24a9cd966` |
| Vulkan-Headers | 1.4.366 | `73814d006638aa00bd553215b054f0f02e457e22` |

Qt modules and Mbed TLS child repositories follow their selected parents'
gitlinks. The [historical 19-repository snapshot](2026-10-28-dependencies.json)
is retained with this record so deleting generated directories does not remove
the recovery input. Replay generates the working manifest at
`out/dependencies/resolved-dependencies.json`; each build preserves its selected
source set and final provenance through the existing build machinery.

The host's anonymous GitHub API quota was exhausted. Upstream release metadata
was read through the connected GitHub interface, and the existing resolver's
selection rules were used with official remote Git refs and source objects.
The unchanged `scripts/resolve-dependencies.py` replayed the selected manifest
and verified every resulting source checkout.

Validation: all 118 dependency-tool regression tests passed. Source replay and
verification passed. C/C++ compilation and Core/Qt runtime validation are left
to the user as requested.

Build from PowerShell with a fresh directory, because old build caches include
the repository's previous drive location and incompatible dependency identities:

```powershell
Set-Location D:\code\AnAnArchiver
python scripts/resolve-dependencies.py --qt --offline --replay-manifest docs/validation/2026-10-28-dependencies.json
./scripts/Invoke-Msvc.ps1 cmake --preset desktop-release -B out/build/desktop-20261028
./scripts/Invoke-Msvc.ps1 cmake --build out/build/desktop-20261028 --parallel 4
./scripts/Invoke-Msvc.ps1 ctest --test-dir out/build/desktop-20261028 --output-on-failure
./scripts/Start-Desktop.ps1 out/build/desktop-20261028
```

The replay step must be repeated after deleting `out`, because that directory
contains the working dependency manifest as well as build outputs. `dist`
contains packaging outputs and is not an input to compilation. To perform a new
dependency update, replace replay with
`cmake -DANANARCHIVER_INIT_QT=ON -P scripts/init-dependencies.cmake`, which queries
upstream again. The historical snapshot does not change the latest-version
policy for new dependency updates.

Recovery validation on 2026-10-29 reconstructed this historical snapshot from
the documented versions and revisions and their matching local Git objects.
The original resolver verified the complete graph before preserving the
snapshot. No new latest-version resolution or compilation is claimed by this
recovery.

This builds the resolved Qt and archive dependencies from source. MSVC x64,
CMake, Ninja, Python, Jinja2, jsonschema, and Git for Windows' Perl were found on
the current machine; the superbuild locates Perl automatically.
