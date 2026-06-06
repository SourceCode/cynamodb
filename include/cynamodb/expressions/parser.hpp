#pragma once

#include <cynamodb/expressions/lexer.hpp>
#include <cynamodb/expressions/ast.hpp>
#include <memory>
#include <expected>
#include <string_view>

namespace cynamodb::expressions {

enum class ParserError {
    UnexpectedToken,
    ExpectedIdentifier,
    ExpectedOperator,
    InvalidExpression
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    std::expected<std::shared_ptr<ASTNode>, ParserError> parse_expression();

private:
    std::vector<Token> tokens_;
    size_t pos_ = 0;

    const Token& peek() const;
    Token consume();
    bool match(TokenType type);
    bool match_keyword(std::string_view keyword);
    
    std::expected<std::shared_ptr<ASTNode>, ParserError> parse_or(size_t depth);
    std::expected<std::shared_ptr<ASTNode>, ParserError> parse_and(size_t depth);
    std::expected<std::shared_ptr<ASTNode>, ParserError> parse_not(size_t depth);
    std::expected<std::shared_ptr<ASTNode>, ParserError> parse_comparison(size_t depth);
    std::expected<std::shared_ptr<ASTNode>, ParserError> parse_primary(size_t depth);
    std::expected<std::shared_ptr<ASTNode>, ParserError> parse_path(PathSegment root, size_t depth);
};

// Parses a standalone document path (e.g. "a.b[0].c", "#x.#y"). Returns nullopt if
// the string is not a single valid path. Used by ProjectionExpression handling.
std::optional<PathNode> parse_single_path(const std::string& text);

} // namespace cynamodb::expressions
