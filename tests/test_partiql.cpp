#include <catch2/catch_test_macros.hpp>
#include <cynamodb/expressions/partiql/parser.hpp>

using namespace cynamodb::expressions::partiql;

TEST_CASE("PartiQL Parser basic", "[expressions][partiql]") {
    SECTION("Basic SELECT") {
        auto res = PartiQLParser::parse("SELECT * FROM \"MyTable\"");
        REQUIRE(res.has_value());
        REQUIRE(std::holds_alternative<SelectStatement>(res->data));
    }

    SECTION("Unsupported statement") {
        auto res = PartiQLParser::parse("DROP TABLE \"MyTable\"");
        REQUIRE(!res.has_value());
        REQUIRE(res.error() == PartiQLError::UnsupportedStatement);
    }
}
