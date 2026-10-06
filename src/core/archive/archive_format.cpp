#include "ananarchiver/archive_format.h"

namespace ananarchiver {

std::string_view archiveFormatName(ArchiveFormat format) noexcept {
    switch (format) {
    case ArchiveFormat::Unknown: return "Unknown";
    case ArchiveFormat::Zip: return "ZIP";
    case ArchiveFormat::SevenZip: return "7z";
    case ArchiveFormat::Rar: return "RAR";
    case ArchiveFormat::Rar5: return "RAR5";
    case ArchiveFormat::Tar: return "TAR";
    case ArchiveFormat::Gzip: return "GZIP";
    case ArchiveFormat::Bzip2: return "BZIP2";
    case ArchiveFormat::Xz: return "XZ";
    case ArchiveFormat::Zstd: return "ZSTD";
    case ArchiveFormat::Lz4: return "LZ4";
    case ArchiveFormat::Cab: return "CAB";
    case ArchiveFormat::Cpio: return "CPIO";
    case ArchiveFormat::Ar: return "AR";
    case ArchiveFormat::Iso9660: return "ISO9660";
    case ArchiveFormat::Lha: return "LHA/LZH";
    case ArchiveFormat::Xar: return "XAR";
    case ArchiveFormat::Other: return "Other";
    }
    return "Unknown";
}

} // namespace ananarchiver
