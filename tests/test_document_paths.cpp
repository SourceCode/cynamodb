// Tests for document-path support (a.b.c, a[0]) across Update/Condition/Filter/
// Projection expressions.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cynamodb/api/handlers.hpp>
#include <cynamodb/engine/memory_engine.hpp>
#include <cynamodb/engine/table_manager.hpp>
#include <cynamodb/expressions/update_expression.hpp>

#include <atomic>
#include <filesystem>
#include <string>

using namespace cynamodb;
using Catch::Matchers::ContainsSubstring;

namespace {
std::string meta_path() {
    static std::atomic<uint64_t> c{0};
    auto dir = std::filesystem::temp_directory_path() / ("cynamodb_path_" + std::to_string(c.fetch_add(1)));
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
std::shared_ptr<core::AttributeValue> S(const std::string& s) {
    auto v = std::make_shared<core::AttributeValue>(); v->type = core::AttributeType::S; v->value = core::String(s); return v;
}
std::shared_ptr<core::AttributeValue> N(const std::string& s) {
    auto v = std::make_shared<core::AttributeValue>(); v->type = core::AttributeType::N; v->value = core::String(s); return v;
}
}  // namespace

TEST_CASE("UpdateExpression SET on a nested map path", "[paths][update]") {
    expressions::ItemMap item;
    auto m = std::make_shared<core::AttributeValue>();
    m->type = core::AttributeType::M;
    core::MapValue inner; inner[core::String("rating")] = N("3");
    m->value = std::move(inner);
    item["info"] = m;

    auto r = expressions::apply_update_expression("SET info.rating = :v", item, {}, {{":v", N("5")}});
    REQUIRE(r.ok);
    REQUIRE(std::get<core::String>(std::get<core::MapValue>(item["info"]->value).at(core::String("rating"))->value) == "5");

    // The original shared value must not be aliased/mutated (copy-on-write).
    REQUIRE(std::get<core::String>(std::get<core::MapValue>(m->value).at(core::String("rating"))->value) == "3");
}

TEST_CASE("UpdateExpression SET appends to a list by index", "[paths][update]") {
    expressions::ItemMap item;
    auto l = std::make_shared<core::AttributeValue>();
    l->type = core::AttributeType::L;
    core::ListValue lv; lv.push_back(S("a"));
    l->value = std::move(lv);
    item["tags"] = l;

    auto r = expressions::apply_update_expression("SET tags[1] = :v", item, {}, {{":v", S("b")}});
    REQUIRE(r.ok);
    const auto& out = std::get<core::ListValue>(item["tags"]->value);
    REQUIRE(out.size() == 2);
    REQUIRE(std::get<core::String>(out[1]->value) == "b");
}

TEST_CASE("UpdateExpression REMOVE a nested map key and a list element", "[paths][update]") {
    expressions::ItemMap item;
    auto m = std::make_shared<core::AttributeValue>();
    m->type = core::AttributeType::M;
    core::MapValue inner; inner[core::String("a")] = N("1"); inner[core::String("b")] = N("2");
    m->value = std::move(inner);
    item["doc"] = m;

    auto r = expressions::apply_update_expression("REMOVE doc.a", item, {}, {});
    REQUIRE(r.ok);
    const auto& mm = std::get<core::MapValue>(item["doc"]->value);
    REQUIRE(mm.find(core::String("a")) == mm.end());
    REQUIRE(mm.find(core::String("b")) != mm.end());
}

TEST_CASE("SET on a missing parent fails loudly", "[paths][update]") {
    expressions::ItemMap item;
    auto r = expressions::apply_update_expression("SET a.b.c = :v", item, {}, {{":v", N("1")}});
    REQUIRE_FALSE(r.ok);
    REQUIRE(item.empty());
}

TEST_CASE("ConditionExpression and ProjectionExpression honor nested paths", "[paths][features]") {
    Harness h;
    h.create();
    h.call(api::Operation::PutItem,
           R"({"TableName":"T","Item":{"pk":{"S":"a"},"info":{"M":{"rating":{"N":"5"},"hidden":{"S":"x"}}},"tags":{"L":[{"S":"t0"},{"S":"t1"}]}}})");

    SECTION("condition on a nested attribute") {
        auto good = h.call(api::Operation::UpdateItem,
                           R"({"TableName":"T","Key":{"pk":{"S":"a"}},)"
                           R"("UpdateExpression":"SET info.rating = :new",)"
                           R"("ConditionExpression":"info.rating = :cur",)"
                           R"("ExpressionAttributeValues":{":new":{"N":"6"},":cur":{"N":"5"}}})");
        REQUIRE(good.status == 200);
        auto bad = h.call(api::Operation::UpdateItem,
                          R"({"TableName":"T","Key":{"pk":{"S":"a"}},)"
                          R"("UpdateExpression":"SET info.rating = :new",)"
                          R"("ConditionExpression":"info.rating = :cur",)"
                          R"("ExpressionAttributeValues":{":new":{"N":"7"},":cur":{"N":"999"}}})");
        REQUIRE(bad.error_type == "ConditionalCheckFailedException");
    }

    SECTION("projection of a nested path returns only the leaf, nested") {
        auto r = h.call(api::Operation::GetItem,
                        R"({"TableName":"T","Key":{"pk":{"S":"a"}},"ProjectionExpression":"info.rating"})");
        REQUIRE(r.status == 200);
        REQUIRE_THAT(r.body, ContainsSubstring("\"info\":{\"M\":{\"rating\":{\"N\":"));
        REQUIRE(r.body.find("hidden") == std::string::npos);
    }

    SECTION("projection of a list index compacts") {
        auto r = h.call(api::Operation::GetItem,
                        R"({"TableName":"T","Key":{"pk":{"S":"a"}},"ProjectionExpression":"tags[1]"})");
        REQUIRE_THAT(r.body, ContainsSubstring("\"tags\":{\"L\":[{\"S\":\"t1\"}]}"));
    }
}

TEST_CASE("attribute_exists on a nested path", "[paths][features]") {
    Harness h;
    h.create();
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"a"},"info":{"M":{"x":{"N":"1"}}}}})");
    auto present = h.call(api::Operation::UpdateItem,
                         R"J({"TableName":"T","Key":{"pk":{"S":"a"}},"UpdateExpression":"SET info.y = :v","ConditionExpression":"attribute_exists(info.x)","ExpressionAttributeValues":{":v":{"N":"2"}}})J");
    REQUIRE(present.status == 200);
    auto absent = h.call(api::Operation::UpdateItem,
                        R"J({"TableName":"T","Key":{"pk":{"S":"a"}},"UpdateExpression":"SET info.z = :v","ConditionExpression":"attribute_exists(info.nope)","ExpressionAttributeValues":{":v":{"N":"3"}}})J");
    REQUIRE(absent.error_type == "ConditionalCheckFailedException");
}
