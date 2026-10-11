# Build dependencies

Use the latest stable release from each dependency's designated upstream when
initializing or updating sources. Do not prescribe a fixed historical version,
tag, or commit. Exclude alpha, beta, RC, and nightly builds by default. If a
project has no maintained releases, use its upstream default branch's latest
commit. For libarchive, the designated upstream remains ananwanan/libarchive.

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
| libarchive | [ananwanan/libarchive](https://github.com/ananwanan/libarchive) | Latest maintained release, or latest default-branch commit if no independent releases exist | Project archive backend |
| zlib | [madler/zlib](https://github.com/madler/zlib) | Latest stable release | Deflate / ZIP / GZIP |
| bzip2 | [libarchive/bzip2](https://github.com/libarchive/bzip2) | Latest stable release | BZIP2 |
| XZ/liblzma | [tukaani-project/xz](https://github.com/tukaani-project/xz) | Latest stable release | LZMA / LZMA2 / XZ / 7z |
| Zstandard | [facebook/zstd](https://github.com/facebook/zstd) | Latest stable release | ZSTD |
| LZ4 | [lz4/lz4](https://github.com/lz4/lz4) | Latest stable release | LZ4 |
| Expat | [libexpat/libexpat](https://github.com/libexpat/libexpat) | Latest stable release | XAR XML metadata |
| Mbed TLS | [Mbed-TLS/mbedtls](https://github.com/Mbed-TLS/mbedtls) | Latest stable release with its matching framework submodule | AES / cryptographic digests without a system OpenSSL dependency |
| Qt supermodule | [qt/qt5](https://github.com/qt/qt5) | Latest stable Qt 6 release with matching qtbase, qtshadertools, qtdeclarative, and qtsvg modules | Desktop frontend source build |
| Vulkan-Headers | [KhronosGroup/Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | Latest stable release | Qt's Vulkan API headers; no Vulkan SDK library is linked |

## Resolve, restore, and verify

The CMake entry point calls `scripts/resolve_dependencies.py` using Python's
standard library and Git. A normal invocation checks the designated upstreams
again, resolves the latest stable source set, fetches and checks out those
revisions, and writes `out/dependency-resolution.json`. It does not initialize
the historical gitlinks and stop there. Local dependency modifications are
protected; resolve conflicts before retrying rather than discarding them.

Initialize the archive dependencies only, or include source Qt:

```sh
cmake -P scripts/init-dependencies.cmake
cmake -DANANARCHIVER_INIT_QT=ON -P scripts/init-dependencies.cmake
```

The second command also obtains the Qt supermodule's matching qtbase,
qtshadertools, qtdeclarative, and qtsvg revisions and Vulkan-Headers. Mbed TLS's
framework is always checked out at the revision selected by the Mbed TLS
supermodule. Nested repositories are recorded as part of the source set, not
upgraded to their individual latest branches.

The schema-versioned manifest records each dependency's source URL, actual
version, release tag when applicable, commit revision, selection rule, UTC
resolution time, and parent relationship. It is a record of one resolution,
not a permanent dependency lock. To use another manifest path, pass
`-DANANARCHIVER_DEPENDENCY_MANIFEST=<path>` to both initialization and CMake
configuration. Keep this JSON with the build artifacts to reproduce a build.

To restore a saved `out/dependency-resolution.json` without selecting newer
versions:

```sh
cmake -DANANARCHIVER_DEPENDENCY_RESTORE=ON -P scripts/init-dependencies.cmake
cmake --preset core-release
cmake --build --preset core-release
ctest --preset core-release
```

For a source Qt build, restore a manifest that includes Qt, add
`-DANANARCHIVER_INIT_QT=ON` to the restore command, and use `desktop-release`.
Restoration fetches the recorded revisions if needed; it does not silently
replace an unavailable revision with a newer or older one. A subsequent normal
initialization still checks for the latest stable sources.

Check the current checkout against the manifest without fetching or updating:

```sh
cmake -DANANARCHIVER_DEPENDENCY_VERIFY=ON -P scripts/init-dependencies.cmake
python -m unittest discover -s tests/dependencies -p 'test_*.py' -v
```

Source Qt verification also requires `-DANANARCHIVER_INIT_QT=ON`. The resolver
regression suite uses local fixture repositories and mocked release metadata;
it does not contact upstreams. GitHub API authentication may use `GITHUB_TOKEN`
or `GH_TOKEN` in the environment. Tokens are used only in request headers and
are never written into manifests, source URLs, or Git configuration.

CMake verifies that the bundled sources match the chosen manifest before
configuration. The superbuild uses those sources without downloading or
updating them during compilation. After dependency changes use a fresh build
directory; a build directory already bound to a different source set is
rejected, so stale installed libraries cannot be reported as the new versions.
Updating resolution timestamps without changing sources or the toolchain does
not invalidate the existing build directory.
`out/build/<preset>/build-info.json` and
`out/build/<preset>/application/build-info.json` record the actual source set
and build environment; installation preserves the record at
`share/ananarchiver/build-info.json`. Explicit developer system dependencies are
identified separately and do not inherit the bundled source provenance.

CI resolves once in a dedicated job and passes the manifest artifact to all
four platform/compiler builds and the sanitizer build. Each restores that
exact source set and uploads its resolution, available build metadata, and
diagnostics even on failure. Resolver tests run independently on Windows and
Linux. Existing reports in `docs/validation` remain historical evidence for
the versions actually tested there.

`cmake/vendor/zlib` and `cmake/vendor/bzip2` contain build descriptions only.
The zlib adapter avoids source-header mutation during dependency configuration.
The current bzip2 source uses the project's CMake build adapter. Codec source code stays
in its original submodule.

An existing Qt kit is available only through the explicit `desktop-developer`
preset and `QTDIR`. `ANANARCHIVER_USE_SYSTEM_LIBARCHIVE=ON` is a separate developer
escape hatch; record that selected backend's source and version separately.

CMake, Ninja/Make, Python, Jinja2, jsonschema, Perl, the C/C++ compiler, and operating-system
SDK/display APIs are build tools and platform prerequisites. Mbed TLS uses
Python/Jinja2/jsonschema and Perl to generate its sources; install the latest stable
Python modules with `python -m pip install --upgrade jinja2 jsonschema`. Prefer the latest stable
tool versions that support the target platform and C++23. Minimum compatibility requirements in
CMake are lower bounds, not fixed dependency versions. Linux Qt needs the platform's X11/XCB or
Wayland development packages; macOS uses the Xcode SDK; Windows uses the Windows
SDK. These platform interfaces are not copied into the application repository.
Optional networking, ICU, CUPS, D-Bus, system image codecs, libxml2, ACL/XAttr,
PCRE, and external archive helper programs are not required by this milestone.
Expat and Mbed TLS keep XAR and encrypted ZIP capabilities available in the
bundled archive backend. Reader/writer format limitations still follow libarchive.

Upstream licenses are retained in each submodule. Refer to their `LICENSE`,
`COPYING`, and Qt `LICENSES/` files for the components being distributed.
