#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cynamodb::utils {

// Standard RFC 4648 base64 (with '+' '/' and '=' padding). DynamoDB transmits
// binary (B/BS) attribute values as base64 strings over the JSON wire protocol,
// so the engine decodes them on ingest and re-encodes them on read.

inline std::string base64_encode(std::string_view data) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
    for (; i + 3 <= data.size(); i += 3) {
        const uint32_t n = (static_cast<uint32_t>(bytes[i]) << 16) |
                           (static_cast<uint32_t>(bytes[i + 1]) << 8) |
                           static_cast<uint32_t>(bytes[i + 2]);
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back(kTable[(n >> 6) & 0x3F]);
        out.push_back(kTable[n & 0x3F]);
    }
    const size_t rem = data.size() - i;
    if (rem == 1) {
        const uint32_t n = static_cast<uint32_t>(bytes[i]) << 16;
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        const uint32_t n = (static_cast<uint32_t>(bytes[i]) << 16) |
                           (static_cast<uint32_t>(bytes[i + 1]) << 8);
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back(kTable[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

inline std::string base64_encode(const std::vector<uint8_t>& data) {
    return base64_encode(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}

template <typename ByteVec>
inline std::string base64_encode_bytes(const ByteVec& data) {
    return base64_encode(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}

// Returns nullopt for malformed input (bad length, stray characters, padding in
// the wrong place). Whitespace is not permitted, matching AWS's strictness.
inline std::optional<std::vector<uint8_t>> base64_decode(std::string_view input) {
    auto decode_char = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };

    if (input.size() % 4 != 0) return std::nullopt;
    std::vector<uint8_t> out;
    out.reserve((input.size() / 4) * 3);
    for (size_t i = 0; i < input.size(); i += 4) {
        const char c0 = input[i];
        const char c1 = input[i + 1];
        const char c2 = input[i + 2];
        const char c3 = input[i + 3];
        const int v0 = decode_char(c0);
        const int v1 = decode_char(c1);
        if (v0 < 0 || v1 < 0) return std::nullopt;

        const bool pad2 = (c2 == '=');
        const bool pad3 = (c3 == '=');
        if (pad2 && !pad3) return std::nullopt;          // "xx=y" is invalid
        if ((pad2 || pad3) && i + 4 != input.size()) {   // padding only in final quad
            return std::nullopt;
        }

        const uint32_t n0 = static_cast<uint32_t>(v0);
        const uint32_t n1 = static_cast<uint32_t>(v1);
        out.push_back(static_cast<uint8_t>((n0 << 2) | (n1 >> 4)));
        if (!pad2) {
            const int v2 = decode_char(c2);
            if (v2 < 0) return std::nullopt;
            const uint32_t n2 = static_cast<uint32_t>(v2);
            out.push_back(static_cast<uint8_t>(((n1 & 0x0F) << 4) | (n2 >> 2)));
            if (!pad3) {
                const int v3 = decode_char(c3);
                if (v3 < 0) return std::nullopt;
                const uint32_t n3 = static_cast<uint32_t>(v3);
                out.push_back(static_cast<uint8_t>(((n2 & 0x03) << 6) | n3));
            }
        }
    }
    return out;
}

}  // namespace cynamodb::utils
