#pragma once

#include "ananarchiver/archive_capabilities.h"
#include "ananarchiver/archive_error.h"
#include "ananarchiver/cancellation.h"
#include "ananarchiver/compression.h"

#include <vector>

namespace ananarchiver {

class ArchiveWriter {
public:
    virtual ~ArchiveWriter() = default;
    virtual std::vector<ArchiveCapabilities> capabilities() const = 0;

    // Synchronous worker API: scanning, payload IO and callbacks run on the
    // caller's thread. Never invoke on the UI thread. One writer belongs to
    // one logical operation. No libarchive handles escape this interface.
    // Success means the completed archive was atomically installed. Existing
    // destination data survives failures/cancellation before that commit.
    virtual Result<void> create(const CompressionRequest& request,
                                const CompressionProgressCallback& progress = {},
                                CancellationToken cancellation = {}) = 0;
};

} // namespace ananarchiver
