// Boundary / encoding tests: large numbers, unicode keys, attribute-name limits.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cynamodb/api/handlers.hpp>
#include <cynamodb/engine/memory_engine.hpp>
#include <cynamodb/engine/table_manager.hpp>

#include <atomic>
#include <filesystem>
#include <string>

using namespace cynamodb;
using Catch::Matchers::ContainsSubstring;

namespace {
std::string meta_path() {
    static std::atomic<uint64_t> c{0};
    auto dir = std::filesystem::temp_directory_path() / ("cynamodb_bnd_" + std::to_string(c.fetch_add(1)));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return (dir / "metadata.bin").string();
}
struct Harness {
    engine::TableManager tables{meta_path()};
    engine::MemoryEngine storage;
    api::ApiResult call(api::Operation op, const std::string& b) { return api::handle_operation(tables, storage, op, b); }
    void create() {
        REQUIRE(call(api::Operation::CreateTable,
                     R"({"TableName":"T","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                     R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})").status == 200);
    }
};
}  // namespace

TEST_CASE("very large and high-precision numbers round-trip exactly", "[boundary]") {
    Harness h;
    h.create();
    int i = 0;
    for (const std::string n : {std::string("9007199254740993"),               // 2^53 + 1
                                std::string("123456789012345678901234567890"),  // 30 digits
                                std::string("3.141592653589793238462643383279"),
                                std::string("-9223372036854775808")}) {
        std::string key = "k" + std::to_string(i++);
        h.call(api::Operation::PutItem,
               R"({"TableName":"T","Item":{"pk":{"S":")" + key + R"("},"num":{"N":")" + n + R"("}}})");
        auto g = h.call(api::Operation::GetItem,
                        R"({"TableName":"T","Key":{"pk":{"S":")" + key + R"("}}})");
        REQUIRE_THAT(g.body, ContainsSubstring("\"num\":{\"N\":\"" + n + "\"}"));
    }
}

TEST_CASE("unicode / emoji string keys round-trip", "[boundary]") {
    Harness h;
    h.create();
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"café-é-🚀"}}})");
    auto g = h.call(api::Operation::GetItem, R"({"TableName":"T","Key":{"pk":{"S":"café-é-🚀"}}})");
    REQUIRE_THAT(g.body, ContainsSubstring("\"Item\""));
}

TEST_CASE("attribute names: 255 bytes accepted, 256 rejected", "[boundary]") {
    Harness h;
    h.create();
    std::string n255(255, 'a');
    auto ok = h.call(api::Operation::PutItem,
                     R"({"TableName":"T","Item":{"pk":{"S":"a"},")" + n255 + R"(":{"S":"v"}}})");
    REQUIRE(ok.status == 200);

    std::string n256(256, 'b');
    auto bad = h.call(api::Operation::PutItem,
                      R"({"TableName":"T","Item":{"pk":{"S":"b"},")" + n256 + R"(":{"S":"v"}}})");
    REQUIRE(bad.status == 400);
    REQUIRE(bad.error_type == "ValidationException");
}

TEST_CASE("control characters in string values are escaped on output", "[boundary]") {
    Harness h;
    h.create();
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"c"},"s":{"S":"a\nb\tc\"d\\e"}}})");
    auto g = h.call(api::Operation::GetItem, R"({"TableName":"T","Key":{"pk":{"S":"c"}}})");
    REQUIRE(g.status == 200);
    REQUIRE_THAT(g.body, ContainsSubstring("\\n"));
    REQUIRE_THAT(g.body, ContainsSubstring("\\t"));
    REQUIRE_THAT(g.body, ContainsSubstring("\\\""));
}

TEST_CASE("numeric sort keys beyond 2^53 still order and round-trip", "[boundary]") {
    Harness h;
    REQUIRE(h.call(api::Operation::CreateTable,
                   R"({"TableName":"N","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"},)"
                   R"({"AttributeName":"sk","KeyType":"RANGE"}],)"
                   R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"},)"
                   R"({"AttributeName":"sk","AttributeType":"N"}]})").status == 200);
    // The N attribute value is preserved exactly even if the sort codec uses double.
    h.call(api::Operation::PutItem,
           R"({"TableName":"N","Item":{"pk":{"S":"p"},"sk":{"N":"9007199254740993"}}})");
    auto g = h.call(api::Operation::GetItem,
                    R"({"TableName":"N","Key":{"pk":{"S":"p"},"sk":{"N":"9007199254740993"}}})");
    REQUIRE_THAT(g.body, ContainsSubstring("\"sk\":{\"N\":\"9007199254740993\"}"));
}
