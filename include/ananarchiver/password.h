#pragma once

#include <string>

namespace ananarchiver {

// Best-effort lifetime cleanup, including exception/cancellation paths. A
// volatile write prevents elision; copies held by codecs remain codec-owned.
inline void scrubPassword(std::string& password) noexcept {
    volatile char* bytes = password.empty() ? nullptr : password.data();
    for (std::size_t i = 0; i < password.size(); ++i) {
        bytes[i] = 0;
    }
    password.clear();
}

} // namespace ananarchiver
