#include <cynamodb/expressions/lexer.hpp>
#include <cctype>
#include <algorithm>
#include <array>

namespace cynamodb::expressions {

namespace {
constexpr size_t kMaxLexerTokens = 4096;
constexpr size_t kMaxExpressionChars = 64 * 1024;
constexpr size_t kMaxTokenBytes = 255;

bool is_identifier_start(unsigned char c) {
    return std::isalpha(c) != 0 || c == '_';
}

bool is_identifier_char(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_';
}
}

Lexer::Lexer(const std::string& input) : input_(input) {}

void Lexer::skip_whitespace() {
    while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_])) != 0) {
        pos_++;
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    tokens.reserve(std::min(kMaxLexerTokens, (input_.size() / 2U) + 2U));
    if (input_.size() > kMaxExpressionChars) {
        tokens.push_back({TokenType::INVALID, "input-too-large"});
        tokens.push_back({TokenType::END_OF_FILE, ""});
        return tokens;
    }
    while (true) {
        if (tokens.size() >= kMaxLexerTokens) {
            tokens.push_back({TokenType::INVALID, "token-limit"});
            tokens.push_back({TokenType::END_OF_FILE, ""});
            break;
        }
        auto token = next_token();
        tokens.push_back(token);
        if (token.type == TokenType::INVALID) {
            tokens.push_back({TokenType::END_OF_FILE, ""});
            break;
        }
        if (token.type == TokenType::END_OF_FILE) break;
    }
    return tokens;
}

Token Lexer::next_token() {
    skip_whitespace();
    if (pos_ >= input_.size()) return {TokenType::END_OF_FILE, ""};

    char c = input_[pos_];
    
    if (c == '#') {
        size_t start = pos_;
        pos_++;
        if (pos_ >= input_.size() || !is_identifier_start(static_cast<unsigned char>(input_[pos_]))) {
            return {TokenType::INVALID, "#"};
        }
        while (pos_ < input_.size() && is_identifier_char(static_cast<unsigned char>(input_[pos_]))) {
            pos_++;
        }
        if ((pos_ - start) > kMaxTokenBytes) {
            return {TokenType::INVALID, "placeholder-name-too-long"};
        }
        return {TokenType::PLACEHOLDER_NAME, input_.substr(start, pos_ - start)};
    }
    
    if (c == ':') {
        size_t start = pos_;
        pos_++;
        if (pos_ >= input_.size() || !is_identifier_start(static_cast<unsigned char>(input_[pos_]))) {
            return {TokenType::INVALID, ":"};
        }
        while (pos_ < input_.size() && is_identifier_char(static_cast<unsigned char>(input_[pos_]))) {
            pos_++;
        }
        if ((pos_ - start) > kMaxTokenBytes) {
            return {TokenType::INVALID, "placeholder-value-too-long"};
        }
        return {TokenType::PLACEHOLDER_VALUE, input_.substr(start, pos_ - start)};
    }

    if (is_identifier_start(static_cast<unsigned char>(c))) {
        size_t start = pos_;
        while (pos_ < input_.size() && is_identifier_char(static_cast<unsigned char>(input_[pos_]))) {
            pos_++;
        }
        if ((pos_ - start) > kMaxTokenBytes) {
            return {TokenType::INVALID, "identifier-too-long"};
        }
        std::string val = input_.substr(start, pos_ - start);
        std::string upper = val;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        static constexpr std::array<std::string_view, 10> kKeywords = {
            "SET", "REMOVE", "ADD", "DELETE", "AND", "OR", "NOT", "BETWEEN", "IN", "ATTRIBUTE_EXISTS"};
        static constexpr std::array<std::string_view, 1> kExtraKeywords = {
            "ATTRIBUTE_NOT_EXISTS"};
        if (std::find(kKeywords.begin(), kKeywords.end(), upper) != kKeywords.end()) {
            return {TokenType::KEYWORD, upper};
        }
        if (std::find(kExtraKeywords.begin(), kExtraKeywords.end(), upper) != kExtraKeywords.end()) {
            return {TokenType::KEYWORD, upper};
        }
        return {TokenType::IDENTIFIER, val};
    }

    if (c == '=') { pos_++; return {TokenType::OPERATOR, "="}; }
    if (c == '<') {
        pos_++;
        if (pos_ < input_.size() && input_[pos_] == '=') { pos_++; return {TokenType::OPERATOR, "<="}; }
        if (pos_ < input_.size() && input_[pos_] == '>') { pos_++; return {TokenType::OPERATOR, "<>"}; }
        return {TokenType::OPERATOR, "<"};
    }
    if (c == '>') {
        pos_++;
        if (pos_ < input_.size() && input_[pos_] == '=') { pos_++; return {TokenType::OPERATOR, ">="}; }
        return {TokenType::OPERATOR, ">"};
    }
    if (c == '(') { pos_++; return {TokenType::OPEN_PAREN, "("}; }
    if (c == ')') { pos_++; return {TokenType::CLOSE_PAREN, ")"}; }
    if (c == ',') { pos_++; return {TokenType::COMMA, ","}; }
    if (c == '.') { pos_++; return {TokenType::DOT, "."}; }
    if (c == '[') {
        size_t start = ++pos_;
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_])) != 0) pos_++;
        if (pos_ == start || pos_ >= input_.size() || input_[pos_] != ']') {
            return {TokenType::INVALID, "["};
        }
        std::string digits = input_.substr(start, pos_ - start);
        pos_++;  // consume ']'
        if (digits.size() > 9) return {TokenType::INVALID, "index-too-large"};
        return {TokenType::INDEX, digits};
    }

    const char bad = input_[pos_++];
    return {TokenType::INVALID, std::string(1, bad)};
}

} // namespace cynamodb::expressions
