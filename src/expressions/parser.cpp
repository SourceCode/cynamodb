#include <cynamodb/expressions/parser.hpp>
#include <algorithm>

namespace cynamodb::expressions {

namespace {
constexpr size_t kMaxParserDepth = 50; // Gate 14: Deep-Nesting Protection
constexpr size_t kMaxParserTokens = 4096;
constexpr size_t kMaxFunctionArgs = 128;
constexpr size_t kMaxIdentifierBytes = 255;

bool is_supported_comparison_operator(std::string_view op) {
    return op == "=" || op == "<>" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

bool is_allowed_function_name(std::string_view name) {
    return name == "ATTRIBUTE_EXISTS" || name == "ATTRIBUTE_NOT_EXISTS" || name == "NOT";
}

// Gate 2: Constant Folding
std::shared_ptr<ASTNode> fold_constants(std::shared_ptr<ASTNode> node) {
    if (!node) return nullptr;

    if (std::holds_alternative<BinaryOpNode>(node->data)) {
        auto& bin = std::get<BinaryOpNode>(node->data);
        bin.left = fold_constants(std::move(bin.left));
        bin.right = fold_constants(std::move(bin.right));

        if (std::holds_alternative<LiteralNode>(bin.left->data) && 
            std::holds_alternative<LiteralNode>(bin.right->data)) {
            // Placeholder for real constant folding logic
            // e.g. "5 + 2" -> LiteralNode{7}
        }
    }
    return node;
}
}

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token& Parser::peek() const {
    if (tokens_.empty()) {
        static const Token eof{TokenType::END_OF_FILE, ""};
        return eof;
    }
    if (pos_ >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_[pos_];
}

Token Parser::consume() {
    if (tokens_.empty()) {
        return {TokenType::END_OF_FILE, ""};
    }
    if (pos_ >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_[pos_++];
}

bool Parser::match(TokenType type) {
    if (peek().type == type) {
        pos_++;
        return true;
    }
    return false;
}

bool Parser::match_keyword(std::string_view keyword) {
    if (peek().type == TokenType::KEYWORD && peek().value == keyword) {
        pos_++;
        return true;
    }
    return false;
}

std::expected<std::shared_ptr<ASTNode>, ParserError> Parser::parse_expression() {
    if (tokens_.size() > kMaxParserTokens) {
        return std::unexpected(ParserError::InvalidExpression);
    }
    if (std::any_of(tokens_.begin(), tokens_.end(), [](const Token& token) {
            return token.type == TokenType::INVALID;
        })) {
        return std::unexpected(ParserError::InvalidExpression);
    }
    if (peek().type == TokenType::KEYWORD &&
        (peek().value == "SET" || peek().value == "REMOVE" || peek().value == "ADD" || peek().value == "DELETE")) {
        consume();
    }
    auto expr_res = parse_or(0);
    if (!expr_res) {
        return expr_res;
    }
    if (peek().type != TokenType::END_OF_FILE) {
        return std::unexpected(ParserError::UnexpectedToken);
    }
    return fold_constants(std::move(*expr_res));
}

std::expected<std::shared_ptr<ASTNode>, ParserError> Parser::parse_or(size_t depth) {
    if (depth > kMaxParserDepth) {
        return std::unexpected(ParserError::InvalidExpression);
    }
    auto left_res = parse_and(depth + 1);
    if (!left_res) {
        return left_res;
    }
    auto left = std::move(*left_res);

    while (match_keyword("OR")) {
        auto right_res = parse_and(depth + 1);
        if (!right_res) {
            return right_res;
        }
        auto node = std::make_unique<ASTNode>();
        node->data = BinaryOpNode{"OR", std::move(left), std::move(*right_res)};
        left = std::move(node);
    }
    return left;
}

std::expected<std::shared_ptr<ASTNode>, ParserError> Parser::parse_and(size_t depth) {
    if (depth > kMaxParserDepth) {
        return std::unexpected(ParserError::InvalidExpression);
    }
    auto left_res = parse_not(depth + 1);
    if (!left_res) {
        return left_res;
    }
    auto left = std::move(*left_res);

    while (match_keyword("AND")) {
        auto right_res = parse_not(depth + 1);
        if (!right_res) {
            return right_res;
        }
        auto node = std::make_unique<ASTNode>();
        node->data = BinaryOpNode{"AND", std::move(left), std::move(*right_res)};
        left = std::move(node);
    }
    return left;
}

std::expected<std::shared_ptr<ASTNode>, ParserError> Parser::parse_not(size_t depth) {
    if (depth > kMaxParserDepth) {
        return std::unexpected(ParserError::InvalidExpression);
    }
    if (match_keyword("NOT")) {
        auto operand = parse_not(depth + 1);
        if (!operand) {
            return operand;
        }
        auto node = std::make_unique<ASTNode>();
        std::vector<std::shared_ptr<ASTNode>> args;
        args.push_back(std::move(*operand));
        node->data = FunctionCallNode{"NOT", std::move(args)};
        return node;
    }
    return parse_comparison(depth + 1);
}

std::expected<std::shared_ptr<ASTNode>, ParserError> Parser::parse_comparison(size_t depth) {
    if (depth > kMaxParserDepth) {
        return std::unexpected(ParserError::InvalidExpression);
    }
    auto left_res = parse_primary(depth + 1);
    if (!left_res) {
        return left_res;
    }
    auto left = std::move(*left_res);

    if (peek().type == TokenType::OPERATOR) {
        auto op_token = consume();
        if (!is_supported_comparison_operator(op_token.value)) {
            return std::unexpected(ParserError::UnexpectedToken);
        }
        auto right_res = parse_primary(depth + 1);
        if (!right_res) {
            return right_res;
        }

        auto node = std::make_unique<ASTNode>();
        node->data = BinaryOpNode{op_token.value, std::move(left), std::move(*right_res)};
        return node;
    }

    return left;
}

std::expected<std::shared_ptr<ASTNode>, ParserError> Parser::parse_primary(size_t depth) {
    if (depth > kMaxParserDepth) {
        return std::unexpected(ParserError::InvalidExpression);
    }
    if (match(TokenType::OPEN_PAREN)) {
        auto expr = parse_or(depth + 1);
        if (!expr) {
            return expr;
        }
        if (!match(TokenType::CLOSE_PAREN)) {
            return std::unexpected(ParserError::UnexpectedToken);
        }
        return expr;
    }

    auto token = consume();
    if (token.type == TokenType::IDENTIFIER || token.type == TokenType::KEYWORD) {
        if (token.value.empty() || token.value.size() > kMaxIdentifierBytes) {
            return std::unexpected(ParserError::InvalidExpression);
        }
        if (token.type == TokenType::KEYWORD && !is_allowed_function_name(token.value)) {
            return std::unexpected(ParserError::UnexpectedToken);
        }
        if (peek().type == TokenType::OPEN_PAREN) {
            consume(); // '('
            std::vector<std::shared_ptr<ASTNode>> args;
            if (peek().type != TokenType::CLOSE_PAREN) {
                while (true) {
                    if (args.size() >= kMaxFunctionArgs) {
                        return std::unexpected(ParserError::InvalidExpression);
                    }
                    auto arg_res = parse_or(depth + 1);
                    if (!arg_res) {
                        return arg_res;
                    }
                    args.push_back(std::move(*arg_res));
                    if (peek().type == TokenType::COMMA) {
                        consume();
                    } else {
                        break;
                    }
                }
            }
            if (!match(TokenType::CLOSE_PAREN)) {
                return std::unexpected(ParserError::UnexpectedToken);
            }
            auto node = std::make_unique<ASTNode>();
            node->data = FunctionCallNode{token.value, std::move(args)};
            return node;
        }
        auto node = std::make_unique<ASTNode>();
        node->data = IdentifierNode{token.value};
        return node;
    }

    if (token.type == TokenType::PLACEHOLDER_NAME || token.type == TokenType::PLACEHOLDER_VALUE) {
        if (token.value.size() > kMaxIdentifierBytes || token.value.size() < 2) {
            return std::unexpected(ParserError::InvalidExpression);
        }
        auto node = std::make_unique<ASTNode>();
        node->data = PlaceholderNode{token.value};
        return node;
    }

    return std::unexpected(ParserError::UnexpectedToken);
}

} // namespace cynamodb::expressions
