// Tests for PartiQL ExecuteStatement / BatchExecuteStatement.
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
    auto dir = std::filesystem::temp_directory_path() / ("cynamodb_pql_" + std::to_string(c.fetch_add(1)));
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
    api::ApiResult exec(const std::string& stmt) {
        return call(api::Operation::ExecuteStatement, R"({"Statement":")" + stmt + R"("})");
    }
};
}  // namespace

TEST_CASE("PartiQL INSERT then SELECT", "[partiql]") {
    Harness h;
    h.create();
    auto ins = h.exec("INSERT INTO T VALUE {'pk': 'a', 'n': 5, 'flag': true}");
    REQUIRE(ins.status == 200);

    auto sel = h.exec("SELECT * FROM T WHERE pk = 'a'");
    REQUIRE(sel.status == 200);
    REQUIRE_THAT(sel.body, ContainsSubstring("\"pk\":{\"S\":\"a\"}"));
    REQUIRE_THAT(sel.body, ContainsSubstring("\"n\":{\"N\":\"5\"}"));
    REQUIRE_THAT(sel.body, ContainsSubstring("\"flag\":{\"BOOL\":true}"));
}

TEST_CASE("PartiQL SELECT with projection and filter", "[partiql]") {
    Harness h;
    h.create();
    h.exec("INSERT INTO T VALUE {'pk': 'a', 'status': 'open', 'secret': 'x'}");
    h.exec("INSERT INTO T VALUE {'pk': 'b', 'status': 'closed'}");

    auto sel = h.exec("SELECT status FROM T WHERE status = 'open'");
    REQUIRE_THAT(sel.body, ContainsSubstring("\"status\":{\"S\":\"open\"}"));
    REQUIRE(sel.body.find("secret") == std::string::npos);
    REQUIRE(sel.body.find("closed") == std::string::npos);
}

TEST_CASE("PartiQL UPDATE and DELETE by key", "[partiql]") {
    Harness h;
    h.create();
    h.exec("INSERT INTO T VALUE {'pk': 'a', 'n': 1}");

    auto upd = h.exec("UPDATE T SET n = 9 WHERE pk = 'a'");
    REQUIRE(upd.status == 200);
    auto sel = h.exec("SELECT * FROM T WHERE pk = 'a'");
    REQUIRE_THAT(sel.body, ContainsSubstring("\"n\":{\"N\":\"9\"}"));

    auto del = h.exec("DELETE FROM T WHERE pk = 'a'");
    REQUIRE(del.status == 200);
    auto sel2 = h.exec("SELECT * FROM T WHERE pk = 'a'");
    REQUIRE_THAT(sel2.body, ContainsSubstring("\"Items\":[]"));
}

TEST_CASE("PartiQL parameters", "[partiql]") {
    Harness h;
    h.create();
    auto ins = h.call(api::Operation::ExecuteStatement,
                      R"({"Statement":"INSERT INTO T VALUE {'pk': ?, 'v': ?}","Parameters":[{"S":"k1"},{"N":"42"}]})");
    REQUIRE(ins.status == 200);
    auto sel = h.call(api::Operation::ExecuteStatement,
                      R"({"Statement":"SELECT * FROM T WHERE pk = ?","Parameters":[{"S":"k1"}]})");
    REQUIRE_THAT(sel.body, ContainsSubstring("\"v\":{\"N\":\"42\"}"));
}

TEST_CASE("PartiQL UPDATE without full key is rejected", "[partiql]") {
    Harness h;
    h.create();
    h.exec("INSERT INTO T VALUE {'pk': 'a', 'n': 1}");
    auto upd = h.exec("UPDATE T SET n = 2 WHERE n = 1");
    REQUIRE(upd.status == 400);
    REQUIRE(upd.error_type == "ValidationException");
}

TEST_CASE("BatchExecuteStatement runs multiple statements", "[partiql]") {
    Harness h;
    h.create();
    auto b = h.call(api::Operation::BatchExecuteStatement,
                    R"({"Statements":[{"Statement":"INSERT INTO T VALUE {'pk':'x'}"},)"
                    R"({"Statement":"INSERT INTO T VALUE {'pk':'y'}"}]})");
    REQUIRE(b.status == 200);
    REQUIRE_THAT(b.body, ContainsSubstring("Responses"));
    REQUIRE(h.exec("SELECT * FROM T WHERE pk = 'x'").body.find("\"x\"") != std::string::npos);
}
