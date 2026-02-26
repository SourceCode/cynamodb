#include <catch2/catch_test_macros.hpp>
#include <cynamodb/expressions/lexer.hpp>
#include <cynamodb/expressions/parser.hpp>
#include <cynamodb/expressions/evaluator.hpp>
#include <cynamodb/core/types.hpp>
#include <memory>
#include <string>
#include <vector>

using namespace cynamodb::expressions;
using namespace cynamodb::core;

TEST_CASE("Lexer basic tokens", "[expressions]") {
    Lexer lexer("pk = :val AND attribute_exists(#name)");
    auto tokens = lexer.tokenize();
    REQUIRE(tokens.size() > 0);
}

TEST_CASE("Parser and Evaluator", "[expressions]") {
    Lexer lexer("a = :val");
    Parser parser(lexer.tokenize());
    auto ast = parser.parse_expression();
    REQUIRE(ast.has_value());

    std::map<std::string, std::shared_ptr<AttributeValue>, StringViewLess> item;
    auto val_a = std::make_shared<AttributeValue>();
    val_a->type = AttributeType::S;
    val_a->value = String("test");
    item["a"] = val_a;

    std::map<std::string, std::shared_ptr<AttributeValue>, StringViewLess> values;
    auto val_v = std::make_shared<AttributeValue>();
    val_v->type = AttributeType::S;
    val_v->value = String("test");
    values[":val"] = val_v;

    std::map<std::string, std::string, StringViewLess> names;

    REQUIRE(Evaluator::evaluate_condition(**ast, item, names, values));
}
