#include "ananarchiver/archive_format.h"
#include "ananarchiver/cancellation.h"
#include "ananarchiver/unicode.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        using namespace ananarchiver;
        check(archiveFormatName(ArchiveFormat::Zip) == "ZIP", "ZIP format name");
        check(archiveFormatName(ArchiveFormat::Rar5) == "RAR5", "RAR5 format name");
        check(archiveFormatName(static_cast<ArchiveFormat>(999)) == "Unknown", "Unknown format guard");
        check(isValidUtf8("plain/ascii.txt"), "ASCII pathname");
        const auto text = std::u8string{u8"安安/可可🐄🐈/こんにちは/한글/café.txt"};
        const std::string unicode{text.begin(), text.end()};
        check(isValidUtf8(unicode), "Unicode pathname");
        check(pathToUtf8(pathFromUtf8(unicode)) == unicode, "Native path Unicode round trip");
        check(!isValidUtf8(std::string{"\xC0\xAF", 2}), "Reject overlong UTF-8");
        check(!isValidUtf8(std::string{"\xED\xA0\x80", 3}), "Reject UTF-8 surrogate");
        check(!isValidUtf8(std::string{"\xF4\x90\x80\x80", 4}), "Reject code point over U+10FFFF");
        check(!isValidUtf8(std::string{"\xF0\x9F", 2}), "Reject truncated UTF-8");
        check(!isValidUtf8(std::string{"\x80", 1}), "Reject lone continuation");
        bool rejectedNul = false;
        try {
            static_cast<void>(pathFromUtf8(std::string{"file\0tail", 9}));
        } catch (const std::invalid_argument&) {
            rejectedNul = true;
        }
        check(rejectedNul, "Reject NUL in native path");
        bool rejectedInvalid = false;
        try {
            static_cast<void>(pathFromUtf8(std::string{"\xFF", 1}));
        } catch (const std::invalid_argument&) {
            rejectedInvalid = true;
        }
        check(rejectedInvalid, "Reject invalid native path encoding");
        std::stop_source source;
        CancellationToken token = source.get_token();
        check(!token.stop_requested(), "Cancellation initially clear");
        source.request_stop();
        check(token.stop_requested(), "Cancellation propagated");
        std::cout << "Core format, Unicode and cancellation checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Core test failure: " << error.what() << '\n';
        return 1;
    }
}
