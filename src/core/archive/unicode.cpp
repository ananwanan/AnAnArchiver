#include "ananarchiver/unicode.h"

#include <cstdint>
#include <stdexcept>

namespace ananarchiver {

bool isValidUtf8(std::string_view text) noexcept {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto first = static_cast<unsigned char>(text[offset++]);
        if (first < 0x80) {
            continue;
        }
        unsigned int continuation = 0;
        std::uint32_t point = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) {
            continuation = 1;
            point = first & 0x1FU;
            minimum = 0x80;
        } else if (first >= 0xE0 && first <= 0xEF) {
            continuation = 2;
            point = first & 0x0FU;
            minimum = 0x800;
        } else if (first >= 0xF0 && first <= 0xF4) {
            continuation = 3;
            point = first & 0x07U;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (continuation > text.size() - offset) {
            return false;
        }
        for (unsigned int i = 0; i < continuation; ++i) {
            const auto next = static_cast<unsigned char>(text[offset++]);
            if ((next & 0xC0U) != 0x80U) {
                return false;
            }
            point = (point << 6U) | (next & 0x3FU);
        }
        if (point < minimum || point > 0x10FFFF ||
            (point >= 0xD800 && point <= 0xDFFF)) {
            return false;
        }
    }
    return true;
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    std::string result{bytes.begin(), bytes.end()};
    if (!isValidUtf8(result)) {
        throw std::invalid_argument("Native path is not valid Unicode");
    }
    return result;
}

std::filesystem::path pathFromUtf8(std::string_view text) {
    if (!isValidUtf8(text) || text.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Path is not valid UTF-8");
    }
    return std::filesystem::path{std::u8string{text.begin(), text.end()}};
}

} // namespace ananarchiver
