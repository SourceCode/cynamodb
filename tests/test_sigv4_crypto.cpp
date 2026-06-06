// Tests for the SHA-256 / HMAC-SHA256 primitives and full SigV4 verification.
#include <catch2/catch_test_macros.hpp>

#include <cynamodb/auth/sigv4.hpp>
#include <cynamodb/auth/credential_store.hpp>
#include <cynamodb/utils/sha256.hpp>

#include <string>

using namespace cynamodb;

TEST_CASE("SHA-256 known-answer vectors", "[auth][sha256]") {
    REQUIRE(utils::sha256_hex("") ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(utils::sha256_hex("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(utils::sha256_hex("The quick brown fox jumps over the lazy dog") ==
            "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST_CASE("HMAC-SHA256 RFC 4231 vectors", "[auth][hmac]") {
    // Test case 1: key = 0x0b * 20, data = "Hi There".
    std::string key(20, '\x0b');
    auto mac = utils::hmac_sha256(key, "Hi There");
    REQUIRE(utils::to_hex(mac.data(), mac.size()) ==
            "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

    // Test case 2: key = "Jefe", data = "what do ya want for nothing?".
    auto mac2 = utils::hmac_sha256(std::string("Jefe"), "what do ya want for nothing?");
    REQUIRE(utils::to_hex(mac2.data(), mac2.size()) ==
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST_CASE("AWS SigV4 derived-signing-key example", "[auth][sigv4]") {
    // The signing-key derivation example from the AWS SigV4 documentation
    // (secret "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY", 20150830/us-east-1/iam).
    const std::string secret = "AWS4wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";
    auto k_date = utils::hmac_sha256(secret, "20150830");
    auto k_region = utils::hmac_sha256(k_date, "us-east-1");
    auto k_service = utils::hmac_sha256(k_region, "iam");
    auto k_signing = utils::hmac_sha256(k_service, "aws4_request");
    REQUIRE(utils::to_hex(k_signing.data(), k_signing.size()) ==
            "c4afb1cc5771d871763a393e44b703571b55cc28424d1a5e86da6ed3c154a4b9");
}

namespace {
// Computes a valid SigV4 signature for a request the same way a client SDK would,
// so the test exercises the verifier against an independently-built signature.
std::string sign(const std::string& secret, const std::string& date, const std::string& region,
                 const std::string& service, const std::string& amz_date,
                 const std::string& canonical_request) {
    std::string scope = date + "/" + region + "/" + service + "/aws4_request";
    std::string sts = "AWS4-HMAC-SHA256\n" + amz_date + "\n" + scope + "\n" +
                      utils::sha256_hex(canonical_request);
    auto k_date = utils::hmac_sha256("AWS4" + secret, date);
    auto k_region = utils::hmac_sha256(k_date, region);
    auto k_service = utils::hmac_sha256(k_region, service);
    auto k_signing = utils::hmac_sha256(k_service, "aws4_request");
    auto sig = utils::hmac_sha256(k_signing, sts);
    return utils::to_hex(sig.data(), sig.size());
}
}  // namespace

TEST_CASE("SigV4Verifier accepts a correctly signed request and rejects tampering", "[auth][sigv4]") {
    const std::string secret = "test-secret";
    const std::string access_key = "AKIDTEST";
    const std::string date = "20260606";
    const std::string amz_date = "20260606T120000Z";
    const std::string region = "us-east-1";
    const std::string service = "dynamodb";
    const std::string body = R"({"TableName":"T"})";
    const std::string host = "localhost:8000";

    // Canonical request matching what the verifier builds (signed headers: host;x-amz-date).
    std::string canonical_headers = "host:" + host + "\nx-amz-date:" + amz_date + "\n";
    std::string canonical_request =
        std::string("POST\n") + "/\n" + "\n" + canonical_headers + "\n" +
        "host;x-amz-date\n" + utils::sha256_hex(body);
    std::string signature = sign(secret, date, region, service, amz_date, canonical_request);

    auth::SigV4Parameters params;
    params.algorithm = "AWS4-HMAC-SHA256";
    params.access_key = access_key;
    params.date = date;
    params.region = region;
    params.service = service;
    params.request_type = "aws4_request";
    params.signed_headers = {"host", "x-amz-date"};
    params.signature = signature;

    auth::SigV4Credential cred;
    cred.access_key = core::String(access_key);
    cred.secret_key = core::String(secret);

    auth::SigV4VerifyRequest vr;
    vr.method = "POST";
    vr.canonical_uri = "/";
    vr.canonical_query_string = "";
    vr.payload = body;
    vr.signed_header_values["host"] = host;
    vr.signed_header_values["x-amz-date"] = amz_date;

    REQUIRE(auth::SigV4Verifier::verify_request(params, cred, vr).has_value());

    SECTION("a tampered signature is rejected") {
        params.signature = std::string(signature.size(), '0');
        auto r = auth::SigV4Verifier::verify_request(params, cred, vr);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error() == auth::SigV4VerifyError::SignatureMismatch);
    }

    SECTION("a tampered body is rejected") {
        vr.payload = R"({"TableName":"OTHER"})";
        REQUIRE_FALSE(auth::SigV4Verifier::verify_request(params, cred, vr).has_value());
    }

    SECTION("wrong secret is rejected") {
        cred.secret_key = core::String("wrong-secret");
        REQUIRE_FALSE(auth::SigV4Verifier::verify_request(params, cred, vr).has_value());
    }
}

TEST_CASE("CredentialStore default and override", "[auth][credentials]") {
    auth::CredentialStore store;
    auto def = store.secret_for(auth::CredentialStore::kDefaultAccessKey);
    REQUIRE(def.has_value());
    REQUIRE(store.secret_for("nope") == std::nullopt);
    store.add_credential("AKID2", "secret2");
    REQUIRE(store.secret_for("AKID2") == "secret2");
}
