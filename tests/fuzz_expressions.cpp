#include <cynamodb/expressions/lexer.hpp>
#include <cynamodb/expressions/parser.hpp>
#include <cynamodb/expressions/evaluator.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    std::string input(reinterpret_cast<const char*>(Data), Size);
    
    // Lexing
    cynamodb::expressions::Lexer lexer(input);
    auto tokens = lexer.tokenize();
    
    // Parsing
    cynamodb::expressions::Parser parser(std::move(tokens));
    auto ast = parser.parse_expression();
    
    if (ast) {
        // Evaluation with a dummy item
        std::map<std::string, std::shared_ptr<cynamodb::core::AttributeValue>, cynamodb::core::StringViewLess> item;
        std::map<std::string, std::string, cynamodb::core::StringViewLess> names;
        std::map<std::string, std::shared_ptr<cynamodb::core::AttributeValue>, cynamodb::core::StringViewLess> values;
        
        try {
            cynamodb::expressions::Evaluator::evaluate_condition(**ast, item, names, values);
        } catch (...) {
            // Ignore expected errors
        }
    }
    
    return 0;
}
