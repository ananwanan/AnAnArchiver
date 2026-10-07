#include "ananarchiver/path_security.h"
#include "ananarchiver/unicode.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ananarchiver {
namespace {

ArchiveError unsafe(std::string message) {
    return {ArchiveErrorCode::UnsafePath, std::move(message), {}, {}};
}

bool reservedName(std::string_view component) {
    auto stem = component.substr(0, component.find('.'));
    while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) stem.remove_suffix(1);
    std::string upper{stem};
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
        return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : static_cast<char>(c);
    });
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL" ||
        upper == "CONIN$" || upper == "CONOUT$") {
        return true;
    }
    if (upper.starts_with("COM") || upper.starts_with("LPT")) {
        const auto suffix = std::string_view{upper}.substr(3);
        return (suffix.size() == 1 && suffix[0] >= '1' && suffix[0] <= '9') ||
               suffix == "\xC2\xB9" || suffix == "\xC2\xB2" || suffix == "\xC2\xB3";
    }
    return false;
}

Result<std::vector<std::string>> components(std::string_view pathname,
                                           std::size_t maxPathBytes,
                                           bool allowParent) {
    if (pathname.size() > maxPathBytes) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                                            "Archive pathname exceeds the limit", {}, {}});
    }
    if (pathname.empty() || !isValidUtf8(pathname)) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidPathEncoding,
                                            "Archive pathname is not valid UTF-8", {}, {}});
    }
    if (pathname.front() == '/' || pathname.front() == '\\') {
        return std::unexpected(unsafe("Absolute archive paths are forbidden"));
    }
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start < pathname.size()) {
        const auto end = pathname.find_first_of("/\\", start);
        auto part = pathname.substr(start, end == std::string_view::npos
                                              ? pathname.size() - start : end - start);
        start = end == std::string_view::npos ? pathname.size() : end + 1;
        if (part.empty() || part == ".") {
            continue;
        }
        if (part == "..") {
            if (!allowParent) {
                return std::unexpected(unsafe("Archive pathname contains parent traversal"));
            }
            result.emplace_back(part);
            continue;
        }
        if (part.back() == '.' || part.back() == ' ' || reservedName(part)) {
            return std::unexpected(unsafe("Archive pathname contains a Windows filename alias"));
        }
        for (const unsigned char c : part) {
            if (c < 0x20 || c == 0x7F || c == ':' || c == '"' || c == '<' ||
                c == '>' || c == '|' || c == '?' || c == '*') {
                return std::unexpected(unsafe("Archive pathname contains forbidden characters"));
            }
        }
        result.emplace_back(part);
    }
    return result;
}

} // namespace

Result<std::filesystem::path> secureArchivePath(std::string_view pathname,
                                              std::size_t maxPathBytes,
                                              std::size_t maxDepth) {
    auto parts = components(pathname, maxPathBytes, false);
    if (!parts) {
        return std::unexpected(std::move(parts.error()));
    }
    if (parts->size() > maxDepth) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                                            "Archive pathname exceeds the depth limit", {}, {}});
    }
    std::filesystem::path result;
    for (const auto& part : *parts) {
        result /= pathFromUtf8(part);
    }
    return result;
}

Result<std::filesystem::path> secureLinkTarget(const std::filesystem::path& link,
                                             std::string_view target,
                                             bool hardlink,
                                             std::size_t maxPathBytes,
                                             std::size_t maxDepth) {
    auto parts = components(target, maxPathBytes, !hardlink);
    if (!parts) {
        return std::unexpected(std::move(parts.error()));
    }
    std::vector<std::filesystem::path> normalized;
    if (!hardlink) {
        for (const auto& part : link.parent_path()) {
            normalized.push_back(part);
        }
    }
    for (const auto& part : *parts) {
        if (part == "..") {
            if (normalized.empty()) {
                return std::unexpected(unsafe("Archive link target escapes the extraction root"));
            }
            normalized.pop_back();
        } else {
            normalized.push_back(pathFromUtf8(part));
        }
    }
    if (normalized.size() > maxDepth) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::ResourceLimitExceeded,
                                            "Archive link target exceeds the depth limit", {}, {}});
    }
    std::filesystem::path result;
    for (const auto& part : normalized) {
        result /= part;
    }
    return result;
}

} // namespace ananarchiver
