// End-to-end test for DynamoDB Streams: records emitted on writes and read back via
// the streams data-plane operations.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cynamodb/api/handlers.hpp>
#include <cynamodb/engine/memory_engine.hpp>
#include <cynamodb/engine/table_manager.hpp>
#include <cynamodb/streams/manager.hpp>

#include <atomic>
#include <filesystem>
#include <regex>
#include <string>

using namespace cynamodb;
using Catch::Matchers::ContainsSubstring;

namespace {
std::string meta_path() {
    static std::atomic<uint64_t> c{0};
    auto dir = std::filesystem::temp_directory_path() / ("cynamodb_str_" + std::to_string(c.fetch_add(1)));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return (dir / "metadata.bin").string();
}
struct Harness {
    engine::TableManager tables{meta_path()};
    engine::MemoryEngine storage;
    streams::StreamManager streams;
    api::ApiResult call(api::Operation op, const std::string& b) {
        return api::handle_operation(tables, storage, op, b, nullptr, &streams);
    }
};
std::string extract(const std::string& body, const std::string& key) {
    std::regex re("\"" + key + "\":\"([^\"]*)\"");
    std::smatch m;
    if (std::regex_search(body, m, re)) return m[1].str();
    return "";
}
}  // namespace

TEST_CASE("writes emit stream records readable via the streams API", "[streams][e2e]") {
    Harness h;
    REQUIRE(h.call(api::Operation::CreateTable,
                   R"({"TableName":"T","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                   R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}],)"
                   R"("StreamSpecification":{"StreamEnabled":true,"StreamViewType":"NEW_AND_OLD_IMAGES"}})").status == 200);

    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"a"},"v":{"N":"1"}}})");
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"a"},"v":{"N":"2"}}})");  // MODIFY
    h.call(api::Operation::DeleteItem, R"({"TableName":"T","Key":{"pk":{"S":"a"}}})");              // REMOVE

    auto ls = h.call(api::Operation::ListStreams, R"({"TableName":"T"})");
    REQUIRE(ls.status == 200);
    std::string arn = extract(ls.body, "StreamArn");
    REQUIRE_FALSE(arn.empty());

    auto ds = h.call(api::Operation::DescribeStream, R"({"StreamArn":")" + arn + R"("})");
    REQUIRE(ds.status == 200);
    std::string shard = extract(ds.body, "ShardId");
    REQUIRE_FALSE(shard.empty());

    auto gsi = h.call(api::Operation::GetShardIterator,
                      R"({"StreamArn":")" + arn + R"(","ShardId":")" + shard +
                          R"(","ShardIteratorType":"TRIM_HORIZON"})");
    REQUIRE(gsi.status == 200);
    std::string iterator = extract(gsi.body, "ShardIterator");
    REQUIRE_FALSE(iterator.empty());

    auto gr = h.call(api::Operation::GetRecords, R"({"ShardIterator":")" + iterator + R"("})");
    REQUIRE(gr.status == 200);
    REQUIRE_THAT(gr.body, ContainsSubstring("\"eventName\":\"INSERT\""));
    REQUIRE_THAT(gr.body, ContainsSubstring("\"eventName\":\"MODIFY\""));
    REQUIRE_THAT(gr.body, ContainsSubstring("\"eventName\":\"REMOVE\""));
    REQUIRE_THAT(gr.body, ContainsSubstring("\"NewImage\""));
    REQUIRE_THAT(gr.body, ContainsSubstring("\"OldImage\""));
}

TEST_CASE("a table without a stream produces no streams", "[streams][e2e]") {
    Harness h;
    h.call(api::Operation::CreateTable,
           R"({"TableName":"NoStream","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
           R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})");
    h.call(api::Operation::PutItem, R"({"TableName":"NoStream","Item":{"pk":{"S":"a"}}})");
    auto ls = h.call(api::Operation::ListStreams, R"({"TableName":"NoStream"})");
    REQUIRE(ls.status == 200);
    REQUIRE_THAT(ls.body, ContainsSubstring("\"Streams\":[]"));
}

TEST_CASE("stream ops are 501 when no stream manager is wired", "[streams][e2e]") {
    engine::TableManager tables{meta_path()};
    engine::MemoryEngine storage;
    auto r = api::handle_operation(tables, storage, api::Operation::ListStreams, "{}");
    REQUIRE(r.status == 501);
}
