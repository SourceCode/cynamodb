#include <catch2/catch_test_macros.hpp>
#include <cynamodb/api/dispatcher.hpp>
#include <string>

using namespace cynamodb::api;

TEST_CASE("Target parsing", "[api]") {
    SECTION("Valid DynamoDB targets") {
        REQUIRE(ApiDispatcher::parse_target("DynamoDB_20120810.GetItem") == Operation::GetItem);
        REQUIRE(ApiDispatcher::parse_target("DynamoDB_20120810.PutItem") == Operation::PutItem);
        REQUIRE(ApiDispatcher::parse_target("DynamoDB_20120810.CreateTable") == Operation::CreateTable);
    }

    SECTION("Valid Streams targets") {
        REQUIRE(ApiDispatcher::parse_target("DynamoDBStreams_20120810.ListStreams") == Operation::ListStreams);
    }

    SECTION("Invalid targets") {
        REQUIRE(ApiDispatcher::parse_target("Invalid.GetItem") == Operation::Unknown);
        REQUIRE(ApiDispatcher::parse_target("DynamoDB_20120810.InvalidOp") == Operation::Unknown);
        REQUIRE(ApiDispatcher::parse_target("") == Operation::Unknown);
    }
}
