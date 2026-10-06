#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ananarchiver {

bool isValidUtf8(std::string_view text) noexcept;

// All native path/UTF-8 conversions share this boundary. Invalid byte paths
// raise std::invalid_argument instead of silently using the process locale.
std::string pathToUtf8(const std::filesystem::path& path);
std::filesystem::path pathFromUtf8(std::string_view text);

} // namespace ananarchiver
