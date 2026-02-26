#pragma once

#include <string>
#include <vector>
#include <variant>

namespace cynamodb::expressions {

enum class TokenType {
    IDENTIFIER,         // attribute name
    PLACEHOLDER_NAME,   // #name
    PLACEHOLDER_VALUE,  // :value
    OPERATOR,           // =, <>, <, <=, >, >=
    KEYWORD,            // SET, REMOVE, ADD, DELETE, AND, OR, NOT, BETWEEN, IN
    OPEN_PAREN,
    CLOSE_PAREN,
    COMMA,
    DOT,
    INVALID,
    END_OF_FILE
};

struct Token {
    TokenType type;
    std::string value;
};

class Lexer {
public:
    explicit Lexer(const std::string& input);
    std::vector<Token> tokenize();

private:
    std::string input_;
    size_t pos_ = 0;

    void skip_whitespace();
    Token next_token();
};

} // namespace cynamodb::expressions
