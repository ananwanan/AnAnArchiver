#pragma once

#include "ananarchiver/archive_error.h"

#include <memory>

namespace ananarchiver::detail {

// libarchive performs some pathname conversions through the caller's native
// LC_CTYPE. Each operation uses a private UTF-8 locale on its worker thread;
// the process-global locale is never changed and the previous locale is restored.
// On Windows this also enables native OEM decoding of legacy ZIP entry names.
class ArchiveUnicodeScope {
public:
    struct Impl;
    explicit ArchiveUnicodeScope(std::unique_ptr<Impl> impl);
    ~ArchiveUnicodeScope();
    ArchiveUnicodeScope(const ArchiveUnicodeScope&) = delete;
    ArchiveUnicodeScope& operator=(const ArchiveUnicodeScope&) = delete;
    static Result<std::unique_ptr<ArchiveUnicodeScope>> enter();
private:
    std::unique_ptr<Impl> impl_;
};

} // namespace ananarchiver::detail
