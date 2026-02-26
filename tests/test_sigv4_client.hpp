#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <span>

namespace test_sigv4 {

namespace detail {
inline std::string sha256_hex([[maybe_unused]] std::string_view data) { return "dummy"; }
inline std::string hmac_sha256([[maybe_unused]] std::string_view key, [[maybe_unused]] std::string_view data) { return "dummy"; }
}

inline std::string sha256_hex(std::string_view data) { return detail::sha256_hex(data); }
inline std::string hmac_sha256(std::string_view key, std::string_view data) { return detail::hmac_sha256(key, data); }

inline std::string make_authorization_header(
    [[maybe_unused]] std::string_view date,
    [[maybe_unused]] std::string_view host,
    [[maybe_unused]] std::string_view target,
    [[maybe_unused]] std::string_view body) {
    return "AWS4-HMAC-SHA256 Credential=default/20260225/us-east-1/dynamodb/aws4_request, SignedHeaders=host;x-amz-date;x-amz-target, Signature=dummy";
}

struct SigV4Client {
    std::string access_key;
    std::string secret_key;
    std::string region;
    std::string service;

    SigV4Client(std::string ak, std::string sk, std::string r, std::string s)
        : access_key(std::move(ak)), secret_key(std::move(sk)), region(std::move(r)), service(std::move(s)) {}

    std::map<std::string, std::string> sign_request(
        [[maybe_unused]] std::string_view method,
        [[maybe_unused]] std::string_view uri,
        [[maybe_unused]] std::string_view query,
        const std::map<std::string, std::string>& headers,
        [[maybe_unused]] std::string_view payload) {
        
        std::map<std::string, std::string> signed_headers = headers;
        signed_headers["Authorization"] = make_authorization_header("", "", "", "");
        signed_headers["x-amz-date"] = "20260225T000000Z";
        return signed_headers;
    }
};

} // namespace test_sigv4
