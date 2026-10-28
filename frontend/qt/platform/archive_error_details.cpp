#include "archive_error_details.h"
#include "path_conversion.h"

#include <ananarchiver/archive_error.h>

namespace ananarchiver::qt {

QString formatArchiveErrorDetails(const ArchiveError& error) {
    auto details = fromUtf8(error.message);
    if (!error.backendMessage.empty())
        details += QChar(u'\n') + fromUtf8(error.backendMessage);
    if (!error.path.empty())
        details += QChar(u'\n') + fromPath(error.path);
    return details;
}

} // namespace ananarchiver::qt
