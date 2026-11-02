#include "base64.h"

#include <cstdint>

namespace ucs {

std::string Base64Encode(const std::string& input) {
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= input.size()) {
        uint32_t n = (static_cast<uint8_t>(input[i]) << 16) |
                     (static_cast<uint8_t>(input[i + 1]) << 8) |
                     static_cast<uint8_t>(input[i + 2]);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += table[(n >> 6) & 0x3F];
        out += table[n & 0x3F];
        i += 3;
    }

    size_t remaining = input.size() - i;
    if (remaining == 1) {
        uint32_t n = static_cast<uint8_t>(input[i]) << 16;
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += "==";
    } else if (remaining == 2) {
        uint32_t n = (static_cast<uint8_t>(input[i]) << 16) | (static_cast<uint8_t>(input[i + 1]) << 8);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += table[(n >> 6) & 0x3F];
        out += "=";
    }

    return out;
}

} // namespace ucs
