#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cynamodb::utils {

namespace detail {

constexpr uint32_t kCrcPoly = 0x82F63B78U;

constexpr uint32_t crc32c_entry(uint32_t index) {
    uint32_t crc = index;
    for (int i = 0; i < 8; ++i) {
        crc = (crc >> 1U) ^ (kCrcPoly & (0U - (crc & 1U)));
    }
    return crc;
}

constexpr std::array<uint32_t, 256> make_crc32c_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < table.size(); ++i) {
        table[i] = crc32c_entry(i);
    }
    return table;
}

inline constexpr auto kCrc32cTable = make_crc32c_table();

} // namespace detail

inline uint32_t crc32c_extend(uint32_t crc, const uint8_t* data, size_t length) {
    crc = ~crc;
    for (size_t i = 0; i < length; ++i) {
        crc = detail::kCrc32cTable[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8U);
    }
    return ~crc;
}

inline uint32_t crc32c(const uint8_t* data, size_t length) {
    return crc32c_extend(0U, data, length);
}

inline uint32_t crc32c(std::string_view data) {
    return crc32c(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

} // namespace cynamodb::utils
