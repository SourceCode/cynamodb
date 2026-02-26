#pragma once

#include <optional>
#include <expected>
#include <memory_resource>
#include <string_view>
#include <cynamodb/core/types.hpp>

namespace cynamodb::auth {

struct SigV4Parameters {
    std::string_view algorithm;
    std::string_view access_key;
    std::string_view date;
    std::string_view region;
    std::string_view service;
    std::string_view request_type;
    core::Vector<std::string_view> signed_headers;
    std::string_view signature;
};

class SigV4Parser {
public:
    static std::optional<SigV4Parameters> parse_authorization_header(std::string_view auth_header);
};

struct SigV4Credential {
    core::String access_key;
    core::String secret_key;
    std::optional<core::String> session_token;
};

enum class SigV4VerifyError {
    InvalidRequest,
    MissingSignedHeader,
    MissingCredential,
    SignatureMismatch,
    SessionTokenMismatch,
    ExpiredRequest
};

struct SigV4VerifyRequest {
    std::string_view method;
    std::string_view canonical_uri;
    std::string_view canonical_query_string;
    std::string_view payload;
    core::Map<std::string_view, std::string_view> signed_header_values;
    std::optional<std::string_view> security_token_header;
};

class SigV4Verifier {
public:
    static std::expected<void, SigV4VerifyError> verify_request(
        const SigV4Parameters& params,
        const SigV4Credential& credential,
        const SigV4VerifyRequest& request);
};

std::string sha256_hex_digest(std::string_view data);

} // namespace cynamodb::auth
