#include "path_conversion.h"
#include <ananarchiver/unicode.h>

namespace ananarchiver::qt {

std::filesystem::path toPath(const QString& text)
{
#ifdef _WIN32
    return std::filesystem::path(text.toStdWString());
#else
    const auto utf8 = text.toUtf8();
    return pathFromUtf8(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
#endif
}

QString fromPath(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.native());
#else
    return fromUtf8(pathToUtf8(path));
#endif
}

QString fromUtf8(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}
