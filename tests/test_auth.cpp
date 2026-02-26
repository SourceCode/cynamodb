#include <catch2/catch_test_macros.hpp>
#include <cynamodb/auth/sigv4.hpp>
#include "test_sigv4_client.hpp"

using namespace cynamodb::auth;

TEST_CASE("SigV4 parser works", "[auth]") {
    std::string auth_header = "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, SignedHeaders=content-type;host;x-amz-date;x-amz-target, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    
    REQUIRE(params.has_value());
    REQUIRE(params->access_key == "AKIAIOSFODNN7EXAMPLE");
    REQUIRE(params->region == "us-east-1");
    REQUIRE(params->service == "dynamodb");
    REQUIRE(params->signature == "fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06");
    REQUIRE(params->signed_headers.size() == 4);
}

TEST_CASE("SigV4 parser rejects malformed signatures", "[auth]") {
    std::string auth_header = "AWS4-HMAC-SHA256 Credential=AKIA/20240101/us-east-1/dynamodb/aws4_request, SignedHeaders=host, Signature=not-hex";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(!params.has_value());
}

TEST_CASE("SigV4 parser requires host in signed headers", "[auth]") {
    std::string auth_header = "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, SignedHeaders=x-amz-date;x-amz-target, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(!params.has_value());
}

TEST_CASE("SigV4 parser rejects non-dynamodb service", "[auth]") {
    std::string auth_header = "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/s3/aws4_request, SignedHeaders=host;x-amz-date, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(!params.has_value());
}

TEST_CASE("SigV4 parser rejects invalid signed header token", "[auth]") {
    std::string auth_header = "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, SignedHeaders=host;x-amz-date;bad_header, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(!params.has_value());
}

TEST_CASE("SigV4 parser rejects malformed algorithm prefix", "[auth]") {
    std::string auth_header = "AWS4-HMAC-SHA256X Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, SignedHeaders=host;x-amz-date, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(!params.has_value());
}

TEST_CASE("SigV4 parser accepts quoted parameter values", "[auth]") {
    std::string auth_header = "AWS4-HMAC-SHA256 Credential=\"AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request\", SignedHeaders=\"content-type;host;x-amz-date;x-amz-target\", Signature=\"fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06\"";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(params.has_value());
    REQUIRE(params->signed_headers.size() == 4);
    REQUIRE(params->signed_headers[0] == "content-type");
}

TEST_CASE("SigV4 parser rejects unsorted signed headers", "[auth]") {
    std::string auth_header =
        "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, "
        "SignedHeaders=x-amz-date;host, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(!params.has_value());
}

TEST_CASE("SigV4 parser requires x-amz-date in signed headers", "[auth]") {
    std::string auth_header =
        "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, "
        "SignedHeaders=host;x-amz-target, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(!params.has_value());
}

TEST_CASE("SigV4 parser rejects duplicate signed headers", "[auth]") {
    std::string auth_header =
        "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, "
        "SignedHeaders=host;host;x-amz-date, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(!params.has_value());
}

TEST_CASE("SigV4 parser rejects invalid access key token", "[auth]") {
    std::string auth_header = "AWS4-HMAC-SHA256 Credential=AKIA_BAD_KEY/20130524/us-east-1/dynamodb/aws4_request, SignedHeaders=host;x-amz-date, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(!params.has_value());
}

TEST_CASE("SigV4 parser rejects short access key", "[auth]") {
    std::string auth_header = "AWS4-HMAC-SHA256 Credential=SHORTKEY123/20130524/us-east-1/dynamodb/aws4_request, SignedHeaders=host;x-amz-date, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(!params.has_value());
}

TEST_CASE("SigV4 parser rejects overly long region", "[auth]") {
    std::string auth_header = "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1-long-region-value-over-thirty-two/dynamodb/aws4_request, SignedHeaders=host;x-amz-date, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(!params.has_value());
}

TEST_CASE("SigV4 parser rejects invalid calendar date", "[auth]") {
    std::string auth_header = "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20230230/us-east-1/dynamodb/aws4_request, SignedHeaders=host;x-amz-date, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(!params.has_value());
}

TEST_CASE("SigV4 parser rejects duplicated signature parameter", "[auth]") {
    std::string auth_header = "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, SignedHeaders=host;x-amz-date, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(!params.has_value());
}

TEST_CASE("SigV4 parser rejects duplicated credential parameter", "[auth]") {
    std::string auth_header = "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, SignedHeaders=host;x-amz-date, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(!params.has_value());
}

TEST_CASE("SigV4 parser rejects unknown authorization parameters", "[auth]") {
    std::string auth_header =
        "AWS4-HMAC-SHA256 XCredential=ignored, Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, "
        "SignedHeaders=content-type;host;x-amz-date;x-amz-target, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE_FALSE(params.has_value());
}

TEST_CASE("SigV4 parser rejects uppercase signature hex", "[auth]") {
    std::string auth_header =
        "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, "
        "SignedHeaders=content-type;host;x-amz-date;x-amz-target, "
        "Signature=FE5F80F77D5FA3BEA14905F2152256B805A9877730E4070A30CA4AF11F681A06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE_FALSE(params.has_value());
}

TEST_CASE("SigV4 parser rejects control characters in header", "[auth]") {
    std::string auth_header =
        "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, "
        "SignedHeaders=host;x-amz-date,\n Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE_FALSE(params.has_value());
}

TEST_CASE("SigV4 parser rejects excessive signed-headers byte length", "[auth]") {
    std::string signed_headers = "host;x-amz-date";
    for (int i = 0; i < 200; ++i) {
        signed_headers += ";x";
        signed_headers += std::string(std::to_string(i));
    }
    std::string auth_header =
        "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, "
        "SignedHeaders=" +
        signed_headers +
        ", Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE_FALSE(params.has_value());
}

TEST_CASE("SigV4 parser rejects non-ascii characters", "[auth]") {
    std::string non_ascii_header = "h";
    non_ascii_header.push_back(static_cast<char>(0xC3));
    non_ascii_header.push_back(static_cast<char>(0xA9));
    non_ascii_header += "ader";
    std::string auth_header =
        "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, "
        "SignedHeaders=host;x-amz-date;" +
        non_ascii_header +
        ", Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE_FALSE(params.has_value());
}

TEST_CASE("SigV4 parser rejects excessive authorization parameter count", "[auth]") {
    std::string extras;
    for (int i = 0; i < 20; ++i) {
        extras += ", P";
        extras += std::string(std::to_string(i));
        extras += "=x";
    }
    std::string auth_header =
        "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, "
        "SignedHeaders=host;x-amz-date, "
        "Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06" +
        extras;
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE_FALSE(params.has_value());
}

TEST_CASE("SigV4 parser requires x-amz-target and content-type in signed headers", "[auth]") {
    std::string auth_header =
        "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, "
        "SignedHeaders=host;x-amz-date, Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE_FALSE(params.has_value());
}

TEST_CASE("SigV4 parser rejects authorization in signed headers", "[auth]") {
    std::string auth_header =
        "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, "
        "SignedHeaders=authorization;content-type;host;x-amz-date;x-amz-target, "
        "Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE_FALSE(params.has_value());
}

TEST_CASE("SigV4 parser rejects leading whitespace in header", "[auth]") {
    std::string auth_header =
        " AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, "
        "SignedHeaders=content-type;host;x-amz-date;x-amz-target, "
        "Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE_FALSE(params.has_value());
}

TEST_CASE("SigV4 verifier validates cryptographic signature", "[auth]") {
    const std::string amz_date = "20260224T010203Z";
    const std::string host = "127.0.0.1:8000";
    const std::string target = "DynamoDB_20120810.ListTables";
    const std::string body = "{}";
    const std::string auth_header = test_sigv4::make_authorization_header(amz_date, host, target, body);
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(params.has_value());

    SigV4Credential credential;
    credential.access_key = "AKIAIOSFODNN7EXAMPLE";
    credential.secret_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";

    SigV4VerifyRequest request;
    request.method = "POST";
    request.canonical_uri = "/";
    request.canonical_query_string = "";
    request.payload = body;
    request.signed_header_values = {
        {"content-type", "application/x-amz-json-1.0"},
        {"host", host},
        {"x-amz-date", amz_date},
        {"x-amz-target", target},
    };

    auto verify_res = SigV4Verifier::verify_request(*params, credential, request);
    REQUIRE(verify_res.has_value());
}

TEST_CASE("SigV4 verifier rejects mismatched scope date", "[auth]") {
    const std::string amz_date = "20260224T010203Z";
    const std::string host = "127.0.0.1:8000";
    const std::string target = "DynamoDB_20120810.ListTables";
    const std::string body = "{}";
    const std::string auth_header = test_sigv4::make_authorization_header(amz_date, host, target, body);
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(params.has_value());
    params->date = "20260223";

    SigV4Credential credential;
    credential.access_key = "AKIAIOSFODNN7EXAMPLE";
    credential.secret_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";

    SigV4VerifyRequest request;
    request.method = "POST";
    request.canonical_uri = "/";
    request.canonical_query_string = "";
    request.payload = body;
    request.signed_header_values = {
        {"content-type", "application/x-amz-json-1.0"},
        {"host", host},
        {"x-amz-date", amz_date},
        {"x-amz-target", target},
    };

    auto verify_res = SigV4Verifier::verify_request(*params, credential, request);
    REQUIRE_FALSE(verify_res.has_value());
    REQUIRE(verify_res.error() == SigV4VerifyError::InvalidRequest);
}

TEST_CASE("SigV4 verifier rejects mutated credential service scope", "[auth]") {
    const std::string amz_date = "20260224T010203Z";
    const std::string host = "127.0.0.1:8000";
    const std::string target = "DynamoDB_20120810.ListTables";
    const std::string body = "{}";
    const std::string auth_header = test_sigv4::make_authorization_header(amz_date, host, target, body);
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(params.has_value());
    params->service = "s3";

    SigV4Credential credential;
    credential.access_key = "AKIAIOSFODNN7EXAMPLE";
    credential.secret_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";

    SigV4VerifyRequest request;
    request.method = "POST";
    request.canonical_uri = "/";
    request.canonical_query_string = "";
    request.payload = body;
    request.signed_header_values = {
        {"content-type", "application/x-amz-json-1.0"},
        {"host", host},
        {"x-amz-date", amz_date},
        {"x-amz-target", target},
    };

    auto verify_res = SigV4Verifier::verify_request(*params, credential, request);
    REQUIRE_FALSE(verify_res.has_value());
    REQUIRE(verify_res.error() == SigV4VerifyError::InvalidRequest);
}

TEST_CASE("SigV4 verifier rejects tampered payload signature", "[auth]") {
    const std::string amz_date = "20260224T010203Z";
    const std::string host = "127.0.0.1:8000";
    const std::string target = "DynamoDB_20120810.ListTables";
    const std::string body = "{}";
    const std::string auth_header = test_sigv4::make_authorization_header(amz_date, host, target, body);
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(params.has_value());

    SigV4Credential credential;
    credential.access_key = "AKIAIOSFODNN7EXAMPLE";
    credential.secret_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";

    SigV4VerifyRequest request;
    request.method = "POST";
    request.canonical_uri = "/";
    request.canonical_query_string = "";
    request.payload = R"({"tampered":true})";
    request.signed_header_values = {
        {"content-type", "application/x-amz-json-1.0"},
        {"host", host},
        {"x-amz-date", amz_date},
        {"x-amz-target", target},
    };

    auto verify_res = SigV4Verifier::verify_request(*params, credential, request);
    REQUIRE_FALSE(verify_res.has_value());
    REQUIRE(verify_res.error() == SigV4VerifyError::SignatureMismatch);
}

TEST_CASE("SigV4 verifier requires matching session token when configured", "[auth]") {
    const std::string amz_date = "20260224T010203Z";
    const std::string host = "127.0.0.1:8000";
    const std::string target = "DynamoDB_20120810.ListTables";
    const std::string body = "{}";
    const std::string auth_header = test_sigv4::make_authorization_header(amz_date, host, target, body);
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(params.has_value());

    SigV4Credential credential;
    credential.access_key = "AKIAIOSFODNN7EXAMPLE";
    credential.secret_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";
    credential.session_token = "token-123";

    SigV4VerifyRequest request;
    request.method = "POST";
    request.canonical_uri = "/";
    request.canonical_query_string = "";
    request.payload = body;
    request.signed_header_values = {
        {"content-type", "application/x-amz-json-1.0"},
        {"host", host},
        {"x-amz-date", amz_date},
        {"x-amz-target", target},
    };

    auto verify_res = SigV4Verifier::verify_request(*params, credential, request);
    REQUIRE_FALSE(verify_res.has_value());
    REQUIRE(verify_res.error() == SigV4VerifyError::SessionTokenMismatch);
}

TEST_CASE("SigV4 parser enforces AKIA or ASIA access-key prefixes", "[auth]") {
    std::string auth_header =
        "AWS4-HMAC-SHA256 Credential=ZZZZIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, "
        "SignedHeaders=content-type;host;x-amz-date;x-amz-target, "
        "Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE_FALSE(params.has_value());
}

TEST_CASE("SigV4 verifier rejects non-uppercase HTTP methods", "[auth]") {
    const std::string amz_date = "20260224T010203Z";
    const std::string host = "127.0.0.1:8000";
    const std::string target = "DynamoDB_20120810.ListTables";
    const std::string body = "{}";
    const std::string auth_header = test_sigv4::make_authorization_header(amz_date, host, target, body);
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(params.has_value());

    SigV4Credential credential;
    credential.access_key = "AKIAIOSFODNN7EXAMPLE";
    credential.secret_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";

    SigV4VerifyRequest request;
    request.method = "post";
    request.canonical_uri = "/";
    request.canonical_query_string = "";
    request.payload = body;
    request.signed_header_values = {
        {"content-type", "application/x-amz-json-1.0"},
        {"host", host},
        {"x-amz-date", amz_date},
        {"x-amz-target", target},
    };

    auto verify_res = SigV4Verifier::verify_request(*params, credential, request);
    REQUIRE_FALSE(verify_res.has_value());
    REQUIRE(verify_res.error() == SigV4VerifyError::InvalidRequest);
}

TEST_CASE("SigV4 sha256 helper is deterministic", "[auth]") {
    const auto hash = sha256_hex_digest("{}");
    REQUIRE(hash == "44136fa355b3678a1146ad16f7e8649e94fb4fc21fe77e8310c060f61caaff8a");
}

TEST_CASE("SigV4 parser rejects whitespace inside credential scope segments", "[auth]") {
    std::string auth_header =
        "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1 /dynamodb/aws4_request, "
        "SignedHeaders=content-type;host;x-amz-date;x-amz-target, "
        "Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE_FALSE(params.has_value());
}

TEST_CASE("SigV4 parser rejects whitespace padded signed header tokens", "[auth]") {
    std::string auth_header =
        "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/dynamodb/aws4_request, "
        "SignedHeaders=content-type; host;x-amz-date;x-amz-target, "
        "Signature=fe5f80f77d5fa3bea14905f2152256b805a9877730e4070a30ca4af11f681a06";
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE_FALSE(params.has_value());
}

TEST_CASE("SigV4 verifier rejects empty canonicalized signed header values", "[auth]") {
    const std::string amz_date = "20260224T010203Z";
    const std::string host = "127.0.0.1:8000";
    const std::string target = "DynamoDB_20120810.ListTables";
    const std::string body = "{}";
    const std::string auth_header = test_sigv4::make_authorization_header(amz_date, host, target, body);
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(params.has_value());

    SigV4Credential credential;
    credential.access_key = "AKIAIOSFODNN7EXAMPLE";
    credential.secret_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";

    SigV4VerifyRequest request;
    request.method = "POST";
    request.canonical_uri = "/";
    request.canonical_query_string = "";
    request.payload = body;
    request.signed_header_values = {
        {"content-type", "application/x-amz-json-1.0"},
        {"host", "   "},
        {"x-amz-date", amz_date},
        {"x-amz-target", target},
    };

    auto verify_res = SigV4Verifier::verify_request(*params, credential, request);
    REQUIRE_FALSE(verify_res.has_value());
    REQUIRE(verify_res.error() == SigV4VerifyError::InvalidRequest);
}

TEST_CASE("SigV4 verifier rejects invalid calendar x-amz-date", "[auth]") {
    const std::string amz_date = "20260224T010203Z";
    const std::string host = "127.0.0.1:8000";
    const std::string target = "DynamoDB_20120810.ListTables";
    const std::string body = "{}";
    const std::string auth_header = test_sigv4::make_authorization_header(amz_date, host, target, body);
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(params.has_value());

    SigV4Credential credential;
    credential.access_key = "AKIAIOSFODNN7EXAMPLE";
    credential.secret_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";

    SigV4VerifyRequest request;
    request.method = "POST";
    request.canonical_uri = "/";
    request.canonical_query_string = "";
    request.payload = body;
    request.signed_header_values = {
        {"content-type", "application/x-amz-json-1.0"},
        {"host", host},
        {"x-amz-date", "20260230T010203Z"},
        {"x-amz-target", target},
    };

    auto verify_res = SigV4Verifier::verify_request(*params, credential, request);
    REQUIRE_FALSE(verify_res.has_value());
    REQUIRE(verify_res.error() == SigV4VerifyError::InvalidRequest);
}

TEST_CASE("SigV4 verifier rejects out-of-range hour in x-amz-date", "[auth]") {
    const std::string amz_date = "20260224T010203Z";
    const std::string host = "127.0.0.1:8000";
    const std::string target = "DynamoDB_20120810.ListTables";
    const std::string body = "{}";
    const std::string auth_header = test_sigv4::make_authorization_header(amz_date, host, target, body);
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(params.has_value());

    SigV4Credential credential;
    credential.access_key = "AKIAIOSFODNN7EXAMPLE";
    credential.secret_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";

    SigV4VerifyRequest request;
    request.method = "POST";
    request.canonical_uri = "/";
    request.canonical_query_string = "";
    request.payload = body;
    request.signed_header_values = {
        {"content-type", "application/x-amz-json-1.0"},
        {"host", host},
        {"x-amz-date", "20260224T240203Z"},
        {"x-amz-target", target},
    };

    auto verify_res = SigV4Verifier::verify_request(*params, credential, request);
    REQUIRE_FALSE(verify_res.has_value());
    REQUIRE(verify_res.error() == SigV4VerifyError::InvalidRequest);
}

TEST_CASE("SigV4 verifier rejects leap-second style x-amz-date values", "[auth]") {
    const std::string amz_date = "20260224T010203Z";
    const std::string host = "127.0.0.1:8000";
    const std::string target = "DynamoDB_20120810.ListTables";
    const std::string body = "{}";
    const std::string auth_header = test_sigv4::make_authorization_header(amz_date, host, target, body);
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(params.has_value());

    SigV4Credential credential;
    credential.access_key = "AKIAIOSFODNN7EXAMPLE";
    credential.secret_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";

    SigV4VerifyRequest request;
    request.method = "POST";
    request.canonical_uri = "/";
    request.canonical_query_string = "";
    request.payload = body;
    request.signed_header_values = {
        {"content-type", "application/x-amz-json-1.0"},
        {"host", host},
        {"x-amz-date", "20260224T010260Z"},
        {"x-amz-target", target},
    };

    auto verify_res = SigV4Verifier::verify_request(*params, credential, request);
    REQUIRE_FALSE(verify_res.has_value());
    REQUIRE(verify_res.error() == SigV4VerifyError::InvalidRequest);
}
