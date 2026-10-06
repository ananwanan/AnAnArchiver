#pragma once

#include "ananarchiver/archive_entry.h"
#include "ananarchiver/archive_error.h"
#include "ananarchiver/archive_format.h"
#include "ananarchiver/cancellation.h"

#include <filesystem>
#include <functional>
#include <string>

namespace ananarchiver {

struct ArchiveInfo {
    std::filesystem::path path;
    ArchiveFormat format = ArchiveFormat::Unknown;
    std::string formatName;
    // True when encryption was identified while probing the first header.
    // Further encrypted entries can be discovered during enumeration.
    bool encrypted = false;
};

using EntryCallback = std::function<bool(const ArchiveEntry&)>;

class ArchiveReader {
public:
    virtual ~ArchiveReader() = default;

    // Each reader belongs to one logical operation and is not thread-safe.
    virtual Result<ArchiveInfo> open(const std::filesystem::path& file,
                                    CancellationToken cancellation = {}) = 0;

    // Reopens the archive and streams metadata from the beginning. The callback
    // runs on the caller's thread, and its entry reference lasts only until it
    // returns. Returning false cancels enumeration. Does not retain file data.
    // Listing is not an integrity test: skipped payloads do not verify CRCs.
    virtual Result<void> enumerate(const EntryCallback& callback,
                                   CancellationToken cancellation = {}) = 0;
};

} // namespace ananarchiver
