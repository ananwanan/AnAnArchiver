#pragma once

#include <string_view>

namespace ananarchiver {

enum class ArchiveFormat {
    Unknown,
    Zip,
    SevenZip,
    Rar,
    Rar5,
    Tar,
    Gzip,
    Bzip2,
    Xz,
    Zstd,
    Lz4,
    Cab,
    Cpio,
    Ar,
    Iso9660,
    Lha,
    Xar,
    Other
};

std::string_view archiveFormatName(ArchiveFormat format) noexcept;

} // namespace ananarchiver
