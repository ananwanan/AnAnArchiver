#pragma once

#include <QString>
#include <filesystem>
#include <string_view>

namespace ananarchiver::qt {

std::filesystem::path toPath(const QString& text);
QString fromPath(const std::filesystem::path& path);
QString fromUtf8(std::string_view text);

}
