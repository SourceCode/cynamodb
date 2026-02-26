#include <catch2/catch_test_macros.hpp>
#include <cynamodb/streams/manager.hpp>

using namespace cynamodb::streams;

TEST_CASE("Streams basic operations", "[streams]") {
    StreamManager manager;

    SECTION("ListStreams") {
        auto res = manager.list_streams(std::nullopt, std::nullopt, 10);
        REQUIRE(res.has_value());
    }
}
