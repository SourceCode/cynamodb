#include <catch2/catch_test_macros.hpp>
#include <cynamodb/auth/sigv4.hpp>

using namespace cynamodb::auth;

TEST_CASE("Zero-copy SigV4 parsing", "[auth][opt]") {
    std::string auth_header = "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20260225/us-east-1/dynamodb/aws4_request, SignedHeaders=host;x-amz-date;x-amz-target, Signature=fe5f80f77d5fa3bea8681d2f02f22468ad161c47c0a604090726756939f84572";
    
    auto params = SigV4Parser::parse_authorization_header(auth_header);
    REQUIRE(params.has_value());
    REQUIRE(params->access_key == "AKIAIOSFODNN7EXAMPLE");
    REQUIRE(params->region == "us-east-1");
    REQUIRE(params->service == "dynamodb");
    REQUIRE(params->signed_headers.size() == 3);
    REQUIRE(params->signed_headers[0] == "host");
    REQUIRE(params->signature == "fe5f80f77d5fa3bea8681d2f02f22468ad161c47c0a604090726756939f84572");
}
