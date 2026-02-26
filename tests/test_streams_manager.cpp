#include <catch2/catch_test_macros.hpp>
#include <cynamodb/streams/manager.hpp>
#include <string>

using namespace cynamodb::streams;
using namespace cynamodb::core;

TEST_CASE("StreamManager basic operations", "[streams]") {
    StreamManager manager;

    SECTION("List streams empty") {
        auto res = manager.list_streams(std::nullopt, std::nullopt, 10);
        REQUIRE(res.has_value());
        REQUIRE(res->streams.empty());
    }
}
