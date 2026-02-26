#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>

namespace cynamodb::engine::lsm {

class BlockedBloomFilter {
public:
    BlockedBloomFilter(size_t num_items, [[maybe_unused]] double fpp) {
        // 10 bits per key for ~1% FPP
        size_t total_bits = static_cast<size_t>(num_items * 10);
        size_t num_blocks = (total_bits + 255) / 256;
        if (num_blocks == 0) num_blocks = 1;
        data_.resize(num_blocks * 8, 0); // 8 uint32_t per 256-bit block
    }

    void add(const std::string& key) {
        uint64_t h = hash(key);
        uint32_t block_idx = (h >> 32) % (data_.size() / 8);
        uint32_t hash_val = static_cast<uint32_t>(h);
        
        uint32_t* block = &data_[block_idx * 8];
        for (int i = 0; i < 8; ++i) {
            uint32_t bit_pos = (hash_val ^ (i * 0x9e3779b9)) % 32;
            block[i] |= (1U << bit_pos);
        }
    }

    bool contains(const std::string& key) const {
        uint64_t h = hash(key);
        uint32_t block_idx = (h >> 32) % (data_.size() / 8);
        uint32_t hash_val = static_cast<uint32_t>(h);
        
        const uint32_t* block = &data_[block_idx * 8];
        for (int i = 0; i < 8; ++i) {
            uint32_t bit_pos = (hash_val ^ (i * 0x9e3779b9)) % 32;
            if (!(block[i] & (1U << bit_pos))) return false;
        }
        return true;
    }

private:
    uint64_t hash(const std::string& key) const {
        uint64_t h = 0x12345678;
        for (char c : key) {
            h = (h ^ static_cast<uint64_t>(c)) * 0xbf58476d1ce4e5b9ULL;
        }
        return h;
    }

    std::vector<uint32_t> data_;
};

} // namespace cynamodb::engine::lsm
