# LiteArc build dependency plan

Use the latest stable release from each dependency's designated upstream when
initializing or updating sources. Do not prescribe a fixed historical version,
tag, or commit. Exclude alpha, beta, RC, and nightly builds by default. If a
project has no maintained releases, use its upstream default branch's latest
commit. The libarchive upstream is the official libarchive/libarchive repository;
select its latest stable release.

Resolve all sources before compiling and use that resolved source set throughout
one build. Record source URLs, actual versions, tags where available, commits,
and resolution time for diagnostics and reproduction of that build. These
records do not prevent selecting newer versions on the next dependency update.
Report resolution or compatibility failures explicitly; do not silently fall
back to an older version while claiming to use the latest.

All archive dependencies are Git submodules. The default CMake superbuild compiles
them into `out/build/<preset>/dependencies/install`; it does not select a codec
from the machine's package search path. Qt is also a Git supermodule and is
built from source for the `desktop-release` preset. Its own bundled third-party
sources (PCRE2, FreeType, HarfBuzz, image codecs, shader compiler, and others) are
selected as part of the latest Qt release's matching module set. Do not update
those nested components independently of that Qt release. Mbed TLS's framework
submodule likewise follows the selected Mbed TLS release.

The source Qt build includes the Basic Quick Controls style used by this frontend;
unneeded alternate style modules are disabled. Its default build directory is
the shorter `out/build/<preset>/qt`, adjustable with
`ANANARCHIVER_QT_BUILD_DIRECTORY`, to leave room for generated MSVC object names.

| Submodule | Upstream | Selection policy | Purpose |
| --- | --- | --- | --- |
| libarchive | [libarchive/libarchive](https://github.com/libarchive/libarchive) | Latest stable official release | Project archive backend |
| zlib | [madler/zlib](https://github.com/madler/zlib) | Latest stable release | Deflate / ZIP / GZIP |
| bzip2 | [libarchive/bzip2](https://github.com/libarchive/bzip2) | Latest stable release | BZIP2 |
| XZ/liblzma | [tukaani-project/xz](https://github.com/tukaani-project/xz) | Latest stable release | LZMA / LZMA2 / XZ / 7z |
| Zstandard | [facebook/zstd](https://github.com/facebook/zstd) | Latest stable release | ZSTD |
| LZ4 | [lz4/lz4](https://github.com/lz4/lz4) | Latest stable release | LZ4 |
| Expat | [libexpat/libexpat](https://github.com/libexpat/libexpat) | Latest stable release | XAR XML metadata |
| Mbed TLS | [Mbed-TLS/mbedtls](https://github.com/Mbed-TLS/mbedtls) | Latest stable release with its matching framework submodule | AES / cryptographic digests without a system OpenSSL dependency |
| Qt supermodule | [qt/qt5](https://github.com/qt/qt5) | Latest stable Qt 6 release with matching qtbase, qtshadertools, qtdeclarative, and qtsvg modules | Desktop frontend source build |
| Vulkan-Headers | [KhronosGroup/Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | Latest stable release | Qt's Vulkan API headers; no Vulkan SDK library is linked |

## Source resolution and replay

`scripts/resolve-dependencies.py` queries the designated upstream release
metadata, excludes drafts and prereleases, and selects the latest stable version.
For projects publishing releases as Git tags instead of GitHub Releases, it
recognizes the upstream's stable version tags. A fork's inherited tags do not
count as independent maintained releases.
For an upstream without independently maintained releases, it selects the latest
default-branch commit. Repository gitlinks record the last resolved checkout;
they do not control a new online resolution. Source selection finishes before
checkout or compilation. Dirty checkouts and metadata, download, or compatibility
failures are errors; the script does not silently downgrade a dependency.

Resolve and initialize only the required sources (avoids cloning all of Qt):

```sh
cmake -P scripts/init-dependencies.cmake
cmake -DANANARCHIVER_INIT_QT=ON -P scripts/init-dependencies.cmake
```

The CMake script invokes Python 3.10 or newer. The first command initializes the
archive dependencies. The second additionally initializes the Qt supermodule,
the four Qt modules, their selected nested sources, and Vulkan-Headers. Mbed TLS
framework and Qt child commits come from the selected parent's Git tree rather
than another latest-version query.

For CI or a review before checkout, resolve a complete source set once and replay
it in every build:

```sh
python scripts/resolve-dependencies.py --qt --resolve-only --manifest out/dependencies/selected.json
python scripts/resolve-dependencies.py --qt --replay-manifest out/dependencies/selected.json
python scripts/resolve-dependencies.py --qt --verify --replay-manifest out/dependencies/selected.json
```

The default output is `out/dependencies/resolved-dependencies.json`. The JSON
schema records `schema_version`, UTC `resolved_at`, `includes_qt`, and a
`dependencies` array. Each entry contains `name`, `path`, upstream `url`, actual
`version`, selected `ref` and `revision`, selection policy, metadata source, and
`coupled_to` for nested components. Preserve the manifest with release artifacts;
it reproduces that build while a future online resolution still selects newer
stable releases.

`--offline` requires `--replay-manifest` and all selected commits already present
locally. It makes no network requests and fails when a selected commit is
missing. `--root` selects a different repository checkout; `--manifest` selects
the output location. `--core-only` replays a full manifest's archive sources
without downloading Qt or Vulkan sources, preserving the selected Qt metadata
for checking an explicit developer kit. The CMake wrapper accepts
`ANANARCHIVER_DEPENDENCY_REPLAY_MANIFEST`, `ANANARCHIVER_DEPENDENCY_MANIFEST`,
`ANANARCHIVER_DEPENDENCY_OFFLINE`, `ANANARCHIVER_DEPENDENCY_CORE_ONLY`,
`ANANARCHIVER_DEPENDENCY_RESOLVE_ONLY`, `ANANARCHIVER_DEPENDENCY_VERIFY`, and
`ANANARCHIVER_PYTHON_EXECUTABLE`.
Use an environment `GITHUB_TOKEN` when the upstream API requires a higher rate
limit; credentials are not recorded in manifests or logs.

`--verify` checks the recorded graph, origins, source versions, revisions, and
local modifications without network access, checkout operations, or manifest
writes. It requires `--replay-manifest`; use `--core-only` for a full manifest
whose Qt sources are intentionally omitted. The offline resolver/provenance
regression suite runs with
`python -m unittest discover -s tests/dependencies -p 'test_*.py' -v`.

CMake verifies checked-out sources against the manifest before building and
copies the actual selected subset to each build's `resolved-dependencies.json`.
`dependency-provenance.json` additionally records project, compiler, libarchive,
and Qt information; installation preserves these files under
`share/AnAnArchiver`. Existing reports in `docs/validation` remain historical
evidence for the source revisions and platforms actually tested there.
Sources are verified again before compilation. Source, toolchain, compile-option,
and build-adapter identities bind the build directory and installed dependency
prefix; incompatible identities require a fresh build directory/prefix, and
unidentified old libraries cannot acquire new revision metadata. Resolution-time
changes alone do not invalidate unchanged sources. Final provenance is recorded
after production targets succeed, including actual source-generator versions.

CI has a single resolution job and replays its uploaded manifest for the Core
compiler matrix, sanitizers, and Windows Qt desktop job. The desktop job builds
the exact resolved Qt release and its matching modules through `desktop-release`,
then runs production QML, archive flows, drag tests, lint, and executable smoke
tests. Core and sanitizer jobs omit Qt source downloads with `--core-only` while
preserving the original selected manifest.
Offline dependency-tool regressions run independently on Windows and Linux.
Replay is followed by pure local verification; each build preserves available
provenance, configure metadata, JUnit/CTest output, and dependency logs on failure.

When an upstream release changes an API before the archive backend adapts to it,
compatibility fixes belong to the backend rather than the Qt frontend. The build
obtains libarchive's latest stable release from its official repository.
`cmake/PrepareLibarchive.cmake` exports the verified official revision with
`git archive` and applies `cmake/patches/libarchive-compatibility.patch` only to
that generated source tree. The patch provides Mbed TLS 4 / TF-PSA-Crypto
compatibility and ZIP abort cleanup. The official submodule remains unchanged.
A patch that no longer applies fails explicitly, requiring review and retirement
of fixes already included upstream; the build does not silently omit fixes or
select an older backend.

Each configure independently reconstructs the expected complete tree from the
official revision and patch. Build verification checks every generated file,
directory and symbolic link against that attestation without rewriting it,
including files untouched by the patch. Edited, missing or added inputs are
rejected before build provenance is recorded.

Build provenance records the official base revision and patch SHA-256. Installation
and distribution packages include the matching patch at
`share/AnAnArchiver/libarchive-compatibility.patch`. To reproduce a build, use the
corresponding application source and recorded patch, replay its official source
manifest, and run CMake normally; the build applies and records the patch
automatically. Source or patch changes require a compatible new build directory
and dependency prefix.

Historical fork fixes and audits are recorded in
`docs/validation/2026-10-11-dependency-resolution.md`; they do not establish that
an official release contains those changes. A source switch requires new source
resolution, build provenance, and regression evidence.

Mbed TLS and TF-PSA-Crypto Git checkouts require generated C/header sources which
release tarballs may already contain. The superbuild enables their upstream
generators, writing output to build directories. Install Perl and current stable
Python `jinja2` and `jsonschema` packages before building; the resolver itself
uses only Python's standard library. Windows can use Git for Windows' Perl by
adding its discovered `usr/bin` directory to the current process PATH, or select
an installed Perl using `PERL_EXECUTABLE`. CI provisions these generator tools
before configuring dependencies.

`cmake/vendor/zlib` and `cmake/vendor/bzip2` contain build descriptions only.
The zlib adapter avoids source-header mutation during dependency configuration.
The current bzip2 source uses the project's CMake build adapter. Codec source code stays
in its original submodule.

An existing Qt kit is available only through the explicit `desktop-developer`
preset and `QTDIR`. `ANANARCHIVER_USE_SYSTEM_LIBARCHIVE=ON` is a separate developer
escape hatch; record that selected backend's source and version separately.

CMake, Ninja/Make, the C/C++ compiler, and operating-system SDK/display APIs are
build tools and platform prerequisites. Prefer their latest stable versions that
support the target platform and C++23. Minimum compatibility requirements in
CMake are lower bounds, not fixed dependency versions. Linux Qt needs the platform's X11/XCB or
Wayland development packages; macOS uses the Xcode SDK; Windows uses the Windows
SDK. These platform interfaces are not copied into the application repository.
Optional networking, ICU, CUPS, D-Bus, system image codecs, libxml2, ACL/XAttr,
PCRE, and external archive helper programs are not required by this milestone.
Expat and Mbed TLS keep XAR and encrypted ZIP capabilities available in the
bundled archive backend. Reader/writer format limitations still follow libarchive.

Upstream licenses are retained in each submodule. Refer to their `LICENSE`,
`COPYING`, and Qt `LICENSES/` files for the components being distributed.

The application help menu shows each component's actual build version and Git
revision when recorded, together with its source repository. These values come
from the verified build identity and configuration shared with final provenance, so a later
submodule update cannot relabel an existing binary. The application is branded
as 轻匣 / LiteArc; `share/AnAnArchiver` remains the compatibility data location.
