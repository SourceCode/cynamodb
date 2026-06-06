#include <cynamodb/auth/sigv4.hpp>
#include <cynamodb/utils/sha256.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace cynamodb::auth {

namespace {

std::string_view trim_view(std::string_view s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    const auto* start = std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !is_space(c); });
    if (start == s.end()) return "";
    const auto* end = std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !is_space(c); }).base();
    return std::string_view(start, end);
}

core::Vector<std::string_view> split_view(std::string_view s, char delim) {
    core::Vector<std::string_view> out;
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(delim, start);
        if (end == std::string_view::npos) {
            out.push_back(trim_view(s.substr(start)));
            break;
        }
        out.push_back(trim_view(s.substr(start, end - start)));
        start = end + 1;
    }
    return out;
}

} // namespace

std::optional<SigV4Parameters> SigV4Parser::parse_authorization_header(std::string_view auth_header) {
    if (!auth_header.starts_with("AWS4-HMAC-SHA256 ")) return std::nullopt;
    
    SigV4Parameters params;
    params.algorithm = "AWS4-HMAC-SHA256";
    
    std::string_view body = auth_header.substr(params.algorithm.size() + 1);
    auto parts = split_view(body, ',');
    
    for (const auto& part : parts) {
        auto eq = part.find('=');
        if (eq == std::string_view::npos) continue;
        
        std::string_view key = trim_view(part.substr(0, eq));
        std::string_view val = trim_view(part.substr(eq + 1));
        
        if (key == "Credential") {
            auto cred_parts = split_view(val, '/');
            if (cred_parts.size() == 5) {
                params.access_key = cred_parts[0];
                params.date = cred_parts[1];
                params.region = cred_parts[2];
                params.service = cred_parts[3];
                params.request_type = cred_parts[4];
            }
        } else if (key == "SignedHeaders") {
            params.signed_headers = split_view(val, ';');
        } else if (key == "Signature") {
            params.signature = val;
        }
    }
    
    if (params.access_key.empty() || params.signature.empty()) return std::nullopt;
    return params;
}

namespace {

std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Collapses runs of internal whitespace and trims, per the SigV4 canonical-header
// value rule (for non-quoted header values).
std::string canonicalize_header_value(std::string_view v) {
    std::string trimmed(trim_view(v));
    std::string out;
    out.reserve(trimmed.size());
    bool in_space = false;
    for (char c : trimmed) {
        if (c == ' ' || c == '\t') {
            in_space = true;
        } else {
            if (in_space && !out.empty()) out.push_back(' ');
            in_space = false;
            out.push_back(c);
        }
    }
    return out;
}

// Constant-time-ish comparison so a verifier does not leak via early-out.
bool secure_equals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return diff == 0;
}

}  // namespace

std::expected<void, SigV4VerifyError> SigV4Verifier::verify_request(
    const SigV4Parameters& params,
    const SigV4Credential& credential,
    const SigV4VerifyRequest& request) {

    if (params.access_key.empty() || params.signature.empty()) {
        return std::unexpected(SigV4VerifyError::MissingCredential);
    }
    if (params.signed_headers.empty()) {
        return std::unexpected(SigV4VerifyError::MissingSignedHeader);
    }
    if (credential.access_key != params.access_key) {
        return std::unexpected(SigV4VerifyError::MissingCredential);
    }

    // 1. Canonical request.
    //    CanonicalHeaders are the signed headers in the order they appear in
    //    SignedHeaders (which the SDK already sorts lexically by lowercase name).
    std::string canonical_headers;
    std::string amz_date;
    for (const auto& h : params.signed_headers) {
        std::string name = to_lower(h);
        auto it = request.signed_header_values.find(name);
        if (it == request.signed_header_values.end()) {
            return std::unexpected(SigV4VerifyError::MissingSignedHeader);
        }
        std::string value = canonicalize_header_value(it->second);
        canonical_headers += name + ":" + value + "\n";
        if (name == "x-amz-date") amz_date = value;
    }
    std::string signed_headers_str;
    for (size_t i = 0; i < params.signed_headers.size(); ++i) {
        if (i) signed_headers_str += ";";
        signed_headers_str += to_lower(params.signed_headers[i]);
    }
    if (amz_date.empty()) {
        return std::unexpected(SigV4VerifyError::MissingSignedHeader);
    }

    const std::string payload_hash = utils::sha256_hex(request.payload);
    std::string canonical_request;
    canonical_request += std::string(request.method) + "\n";
    canonical_request += std::string(request.canonical_uri.empty() ? "/" : request.canonical_uri) + "\n";
    canonical_request += std::string(request.canonical_query_string) + "\n";
    canonical_request += canonical_headers + "\n";
    canonical_request += signed_headers_str + "\n";
    canonical_request += payload_hash;

    // 2. String to sign.
    const std::string scope = std::string(params.date) + "/" + std::string(params.region) +
                              "/" + std::string(params.service) + "/" + std::string(params.request_type);
    std::string string_to_sign;
    string_to_sign += "AWS4-HMAC-SHA256\n";
    string_to_sign += amz_date + "\n";
    string_to_sign += scope + "\n";
    string_to_sign += utils::sha256_hex(canonical_request);

    // 3. Derive signing key and compute the signature.
    const std::string k_secret = "AWS4" + std::string(credential.secret_key);
    auto k_date = utils::hmac_sha256(k_secret, std::string(params.date));
    auto k_region = utils::hmac_sha256(k_date, std::string(params.region));
    auto k_service = utils::hmac_sha256(k_region, std::string(params.service));
    auto k_signing = utils::hmac_sha256(k_service, std::string(params.request_type));
    auto sig = utils::hmac_sha256(k_signing, string_to_sign);
    const std::string computed = utils::to_hex(sig.data(), sig.size());

    if (!secure_equals(computed, params.signature)) {
        return std::unexpected(SigV4VerifyError::SignatureMismatch);
    }
    return {};
}

std::string sha256_hex_digest(std::string_view data) {
    return utils::sha256_hex(data);
}

} // namespace cynamodb::auth
