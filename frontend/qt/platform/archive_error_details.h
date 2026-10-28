#pragma once

#include <QString>

namespace ananarchiver {
struct ArchiveError;
}

namespace ananarchiver::qt {

QString formatArchiveErrorDetails(const ArchiveError& error);

} // namespace ananarchiver::qt
