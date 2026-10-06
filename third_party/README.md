# Pinned build dependencies

All archive dependencies are Git submodules. The default CMake superbuild compiles
them into `out/build/<preset>/dependencies/install`; it does not select a codec
from the machine's package search path. Qt is also a pinned Git supermodule and is
built from source for the `desktop-release` preset. Its own bundled third-party
sources (PCRE2, FreeType, HarfBuzz, image codecs, shader compiler, and others) are
part of the pinned Qt module revisions.

The source Qt build includes the Basic Quick Controls style used by this frontend;
unneeded alternate style modules are disabled. Its default build directory is
the shorter `out/build/<preset>/qt`, adjustable with
`ANANARCHIVER_QT_BUILD_DIRECTORY`, to leave room for generated MSVC object names.

| Submodule | Revision | Purpose |
| --- | --- | --- |
| libarchive | `8bb3bbdc7b117a1e22086a2260f2087aafa90687` | Project archive backend from ananwanan/libarchive |
| zlib 1.3.2 | `da607da739fa6047df13e66a2af6b8bec7c2a498` | Deflate / ZIP / GZIP |
| bzip2 1.0.8 | `6a8690fc8d26c815e798c588f796eabe9d684cf0` | BZIP2 |
| XZ 5.8.4 | `d3e650e63c110e830fd5391e7f8b45df0b91d3da` | LZMA / LZMA2 / XZ / 7z |
| Zstandard 1.5.7 | `f8745da6ff1ad1e7bab384bd1f9d742439278e99` | ZSTD |
| LZ4 1.10.0 | `ebb370ca83af193212df4dcbadcc5d87bc0de2f0` | LZ4 |
| Expat 2.8.5 | `4b3f0b06f39fb5529cead381694f8929901bc273` | XAR XML metadata |
| Mbed TLS 3.6.7 | `068ff080b369adfac81509f9b57b2afabaf82dc5` | AES / cryptographic digests without a system OpenSSL dependency |
| Qt 6.10.3 supermodule | `6417867c2da7aa2b1b94fb9ab084b514fb7009bb` | Desktop frontend source build |
| Vulkan-Headers SDK 1.4.357.0 | `e3b1eec08173d6b825cd3ac88c885a63b621504a` | Qt's Vulkan API headers; no Vulkan SDK library is linked |

The Qt supermodule pins the selected modules:

| Qt module | Revision |
| --- | --- |
| qtbase | `7ddbc87d8e14ce51d2957ea72d0a6077593d5ff4` |
| qtshadertools | `723bea08b53fe20acf026bce1a31168f202c5b6a` |
| qtdeclarative | `3a714efe0672ae1ba06f09864f64957560c69b91` |
| qtsvg | `77765f981c067cec8f250e13688e5be75ee94a1b` |

Mbed TLS pins its framework submodule at
`dde0c4a0e448a0552f18817dcea633bb851fd288`.

Initialize only the required sources (avoids cloning all of Qt):

```sh
cmake -P scripts/init-dependencies.cmake
cmake -DANANARCHIVER_INIT_QT=ON -P scripts/init-dependencies.cmake
```

The first command initializes the archive dependencies. The second additionally
initializes the four Qt modules. All checkouts use the commits recorded by the
parent repository; no build follows a mutable branch or downloads dependencies.

`cmake/vendor/zlib` and `cmake/vendor/bzip2` contain build descriptions only.
The zlib adapter avoids source-header mutation during dependency configuration.
The bzip2 1.0.8 release has no upstream CMake build. Codec source code stays
in its original submodule.

An existing Qt kit is available only through the explicit `desktop-developer`
preset and `QTDIR`. `ANANARCHIVER_USE_SYSTEM_LIBARCHIVE=ON` is a separate developer
escape hatch; it forfeits the bundled-backend reproducibility guarantee.

CMake, Ninja/Make, the C/C++ compiler, and operating-system SDK/display APIs are
build tools and platform prerequisites. Linux Qt needs the platform's X11/XCB or
Wayland development packages; macOS uses the Xcode SDK; Windows uses the Windows
SDK. These platform interfaces are not copied into the application repository.
Optional networking, ICU, CUPS, D-Bus, system image codecs, libxml2, ACL/XAttr,
PCRE, and external archive helper programs are not required by this milestone.
Expat and Mbed TLS keep XAR and encrypted ZIP capabilities available in the
bundled archive backend. Reader/writer format limitations still follow libarchive.

Upstream licenses are retained in each submodule. Refer to their `LICENSE`,
`COPYING`, and Qt `LICENSES/` files for the components being distributed.
