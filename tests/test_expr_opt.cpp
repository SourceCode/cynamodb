#include <catch2/catch_test_macros.hpp>
#include <cynamodb/expressions/lexer.hpp>
#include <cynamodb/expressions/parser.hpp>
#include <cynamodb/expressions/evaluator.hpp>
#include <string>

using namespace cynamodb::expressions;

TEST_CASE("Expression deep nesting protection", "[expressions]") {
    std::string expr = "a=b";
    for (int i = 0; i < 60; ++i) {
        expr = "(" + expr + ")";
    }
    
    Lexer lexer(expr);
    Parser parser(lexer.tokenize());
    auto res = parser.parse_expression();
    
    REQUIRE_FALSE(res.has_value());
}

TEST_CASE("Short-circuit evaluation", "[expressions]") {
    // We can't easily "prove" it didn't evaluate the second part without side effects,
    // but we can verify it still returns the correct result.
    std::map<std::string, std::shared_ptr<cynamodb::core::AttributeValue>, cynamodb::core::StringViewLess> item;
    std::map<std::string, std::string, cynamodb::core::StringViewLess> names;
    std::map<std::string, std::shared_ptr<cynamodb::core::AttributeValue>, cynamodb::core::StringViewLess> values;

    Lexer lexer("attribute_exists(a) OR attribute_exists(b)");
    Parser parser(lexer.tokenize());
    auto ast = parser.parse_expression();
    REQUIRE(ast.has_value());

    auto val_a = std::make_shared<cynamodb::core::AttributeValue>();
    val_a->type = cynamodb::core::AttributeType::S;
    val_a->value = cynamodb::core::String("val");
    item["a"] = val_a;

    // a exists, so it should short-circuit and not even look for b
    REQUIRE(Evaluator::evaluate_condition(**ast, item, names, values));
}
