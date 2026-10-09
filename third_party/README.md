# Build dependency plan

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

## Current initialization behavior

This is a plan change. The current initialization script still checks out the
commits recorded by the parent repository's gitlinks; the superbuild compiles
those local sources without downloading or updating them. Automatic resolution
of the latest upstream versions is not yet implemented. The commands below
initialize the recorded source snapshot and do not upgrade it to the latest.

When implementing the revised policy, update source resolution and CI together,
preserve matching nested module sets, record the resolved revisions, and run the
affected builds and regression tests. Existing reports in `docs/validation`
remain historical evidence for the versions actually tested there.

Initialize only the required sources (avoids cloning all of Qt):

```sh
cmake -P scripts/init-dependencies.cmake
cmake -DANANARCHIVER_INIT_QT=ON -P scripts/init-dependencies.cmake
```

The first command initializes the archive dependencies. The second additionally
initializes the four Qt modules.

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
