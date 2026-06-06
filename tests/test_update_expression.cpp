// Direct tests for the UpdateExpression applier (SET / REMOVE / ADD / DELETE).
#include <catch2/catch_test_macros.hpp>

#include <cynamodb/expressions/update_expression.hpp>

using namespace cynamodb;
using expressions::apply_update_expression;

namespace {
std::shared_ptr<core::AttributeValue> S(const std::string& s) {
    auto v = std::make_shared<core::AttributeValue>();
    v->type = core::AttributeType::S;
    v->value = core::String(s);
    return v;
}
std::shared_ptr<core::AttributeValue> N(const std::string& s) {
    auto v = std::make_shared<core::AttributeValue>();
    v->type = core::AttributeType::N;
    v->value = core::String(s);
    return v;
}
const core::String& as_str(const std::shared_ptr<core::AttributeValue>& v) {
    return std::get<core::String>(v->value);
}
}  // namespace

TEST_CASE("SET assigns and supports arithmetic", "[update]") {
    expressions::ItemMap item;
    item["n"] = N("10");
    expressions::ValueMap values{{":d", N("5")}, {":t", S("hi")}};

    auto r = apply_update_expression("SET n = n + :d, title = :t", item, {}, values);
    REQUIRE(r.ok);
    REQUIRE(as_str(item["n"]) == "15");
    REQUIRE(as_str(item["title"]) == "hi");
}

TEST_CASE("SET subtraction and if_not_exists", "[update]") {
    expressions::ItemMap item;
    item["n"] = N("10");
    expressions::ValueMap values{{":d", N("3")}, {":def", N("100")}};

    REQUIRE(apply_update_expression("SET n = n - :d", item, {}, values).ok);
    REQUIRE(as_str(item["n"]) == "7");

    REQUIRE(apply_update_expression("SET created = if_not_exists(created, :def)", item, {}, values).ok);
    REQUIRE(as_str(item["created"]) == "100");
    // Second call must keep the existing value.
    expressions::ValueMap v2{{":def", N("999")}};
    REQUIRE(apply_update_expression("SET created = if_not_exists(created, :def)", item, {}, v2).ok);
    REQUIRE(as_str(item["created"]) == "100");
}

TEST_CASE("ADD increments and creates counters", "[update]") {
    expressions::ItemMap item;
    expressions::ValueMap values{{":one", N("1")}};
    REQUIRE(apply_update_expression("ADD count :one", item, {}, values).ok);
    REQUIRE(as_str(item["count"]) == "1");
    REQUIRE(apply_update_expression("ADD count :one", item, {}, values).ok);
    REQUIRE(as_str(item["count"]) == "2");
}

TEST_CASE("REMOVE deletes attributes", "[update]") {
    expressions::ItemMap item;
    item["a"] = S("x");
    item["b"] = S("y");
    REQUIRE(apply_update_expression("REMOVE a", item, {}, {}).ok);
    REQUIRE(item.find("a") == item.end());
    REQUIRE(item.find("b") != item.end());
}

TEST_CASE("ExpressionAttributeNames resolve in paths", "[update]") {
    expressions::ItemMap item;
    expressions::NameMap names{{"#s", "status"}};
    expressions::ValueMap values{{":v", S("open")}};
    REQUIRE(apply_update_expression("SET #s = :v", item, names, values).ok);
    REQUIRE(as_str(item["status"]) == "open");
}

TEST_CASE("malformed update expressions fail loudly", "[update]") {
    expressions::ItemMap item;
    REQUIRE_FALSE(apply_update_expression("SET a =", item, {}, {}).ok);
    REQUIRE_FALSE(apply_update_expression("SET a = :missing", item, {}, {}).ok);
    REQUIRE_FALSE(apply_update_expression("FOO a = :v", item, {}, {{":v", S("x")}}).ok);
    // A failure must leave the item untouched.
    REQUIRE(item.empty());
}
