#pragma once

// Self-contained SHA-256 and HMAC-SHA256. cynamoDB links no external crypto
// library, and these are needed for AWS SigV4 verification. The implementation is
// the standard FIPS 180-4 construction; it is not constant-time, which is
// acceptable for a local development/testing engine.

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace cynamodb::utils {

class Sha256 {
public:
    Sha256() { reset(); }

    void reset() {
        state_ = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                  0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
        bitlen_ = 0;
        buflen_ = 0;
    }

    void update(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            buffer_[buflen_++] = data[i];
            if (buflen_ == 64) {
                transform();
                bitlen_ += 512;
                buflen_ = 0;
            }
        }
    }
    void update(std::string_view data) {
        update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }

    std::array<uint8_t, 32> digest() {
        std::array<uint8_t, 32> out{};
        uint64_t total_bits = bitlen_ + static_cast<uint64_t>(buflen_) * 8;
        size_t i = buflen_;
        buffer_[i++] = 0x80;
        if (i > 56) {
            while (i < 64) buffer_[i++] = 0x00;
            transform();
            i = 0;
        }
        while (i < 56) buffer_[i++] = 0x00;
        for (int j = 7; j >= 0; --j) {
            buffer_[i++] = static_cast<uint8_t>((total_bits >> (j * 8)) & 0xFF);
        }
        transform();
        for (int k = 0; k < 8; ++k) {
            out[k * 4 + 0] = static_cast<uint8_t>((state_[k] >> 24) & 0xFF);
            out[k * 4 + 1] = static_cast<uint8_t>((state_[k] >> 16) & 0xFF);
            out[k * 4 + 2] = static_cast<uint8_t>((state_[k] >> 8) & 0xFF);
            out[k * 4 + 3] = static_cast<uint8_t>(state_[k] & 0xFF);
        }
        return out;
    }

private:
    static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

    void transform() {
        static constexpr std::array<uint32_t, 64> k = {
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
            0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
            0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
            0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
            0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
            0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

        std::array<uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(buffer_[i * 4]) << 24) |
                   (static_cast<uint32_t>(buffer_[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(buffer_[i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(buffer_[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = h + s1 + ch + k[i] + w[i];
            uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = s0 + maj;
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    std::array<uint32_t, 8> state_{};
    std::array<uint8_t, 64> buffer_{};
    uint64_t bitlen_ = 0;
    size_t buflen_ = 0;
};

inline std::array<uint8_t, 32> sha256(std::string_view data) {
    Sha256 h;
    h.update(data);
    return h.digest();
}

inline std::string to_hex(const uint8_t* data, size_t len) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(kHex[(data[i] >> 4) & 0xF]);
        out.push_back(kHex[data[i] & 0xF]);
    }
    return out;
}

inline std::string sha256_hex(std::string_view data) {
    auto d = sha256(data);
    return to_hex(d.data(), d.size());
}

// HMAC-SHA256 returning the raw 32-byte MAC.
inline std::array<uint8_t, 32> hmac_sha256(const uint8_t* key, size_t key_len, std::string_view msg) {
    std::array<uint8_t, 64> block{};
    if (key_len > 64) {
        auto hk = sha256(std::string_view(reinterpret_cast<const char*>(key), key_len));
        std::memcpy(block.data(), hk.data(), hk.size());
    } else {
        std::memcpy(block.data(), key, key_len);
    }
    std::array<uint8_t, 64> ipad{};
    std::array<uint8_t, 64> opad{};
    for (size_t i = 0; i < 64; ++i) {
        ipad[i] = block[i] ^ 0x36;
        opad[i] = block[i] ^ 0x5c;
    }
    Sha256 inner;
    inner.update(ipad.data(), ipad.size());
    inner.update(msg);
    auto inner_digest = inner.digest();

    Sha256 outer;
    outer.update(opad.data(), opad.size());
    outer.update(inner_digest.data(), inner_digest.size());
    return outer.digest();
}

inline std::array<uint8_t, 32> hmac_sha256(std::string_view key, std::string_view msg) {
    return hmac_sha256(reinterpret_cast<const uint8_t*>(key.data()), key.size(), msg);
}

inline std::array<uint8_t, 32> hmac_sha256(const std::array<uint8_t, 32>& key, std::string_view msg) {
    return hmac_sha256(key.data(), key.size(), msg);
}

}  // namespace cynamodb::utils
