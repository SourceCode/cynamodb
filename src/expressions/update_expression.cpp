#include <cynamodb/expressions/update_expression.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>

namespace cynamodb::expressions {

namespace {

using core::AttributeType;
using core::AttributeValue;

// ---- Tokenizer ----------------------------------------------------------
// The shared expression Lexer does not emit '+' / '-', which UpdateExpressions
// need, so update expressions get their own small, self-contained tokenizer.

enum class TT { Ident, Name, Value, Op, LParen, RParen, Comma, Keyword, End, Bad };

struct Tok {
    TT type;
    std::string text;
};

bool ident_start(unsigned char c) { return std::isalpha(c) != 0 || c == '_'; }
bool ident_char(unsigned char c) { return std::isalnum(c) != 0 || c == '_'; }

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool is_section_keyword(const std::string& up) {
    return up == "SET" || up == "REMOVE" || up == "ADD" || up == "DELETE";
}

std::vector<Tok> tokenize(const std::string& in, bool& bad) {
    std::vector<Tok> out;
    bad = false;
    size_t i = 0;
    const size_t kMaxTokens = 8192;
    while (i < in.size()) {
        char c = in[i];
        if (std::isspace(static_cast<unsigned char>(c)) != 0) { ++i; continue; }
        if (out.size() > kMaxTokens) { bad = true; break; }
        if (c == '#') {
            size_t start = i++;
            if (i >= in.size() || !ident_start(static_cast<unsigned char>(in[i]))) { bad = true; break; }
            while (i < in.size() && ident_char(static_cast<unsigned char>(in[i]))) ++i;
            out.push_back({TT::Name, in.substr(start, i - start)});
        } else if (c == ':') {
            size_t start = i++;
            if (i >= in.size() || !ident_start(static_cast<unsigned char>(in[i]))) { bad = true; break; }
            while (i < in.size() && ident_char(static_cast<unsigned char>(in[i]))) ++i;
            out.push_back({TT::Value, in.substr(start, i - start)});
        } else if (ident_start(static_cast<unsigned char>(c))) {
            size_t start = i;
            while (i < in.size() && (ident_char(static_cast<unsigned char>(in[i])))) ++i;
            std::string word = in.substr(start, i - start);
            if (is_section_keyword(upper(word))) {
                out.push_back({TT::Keyword, upper(word)});
            } else {
                out.push_back({TT::Ident, word});
            }
        } else if (c == '=' || c == '+' || c == '-') {
            out.push_back({TT::Op, std::string(1, c)});
            ++i;
        } else if (c == '(') { out.push_back({TT::LParen, "("}); ++i; }
        else if (c == ')') { out.push_back({TT::RParen, ")"}); ++i; }
        else if (c == ',') { out.push_back({TT::Comma, ","}); ++i; }
        else if (c == '.' || c == '[' || c == ']') {
            // Document paths / list indexes are not yet supported.
            bad = true; break;
        } else { bad = true; break; }
    }
    out.push_back({TT::End, ""});
    return out;
}

// ---- Numeric helpers ----------------------------------------------------

bool parse_num(const core::String& s, long double& out) {
    std::string tmp(s.data(), s.size());
    if (tmp.empty()) return false;
    char* end = nullptr;
    out = std::strtold(tmp.c_str(), &end);
    return end == tmp.c_str() + tmp.size();
}

std::shared_ptr<AttributeValue> make_number(long double n) {
    auto av = std::make_shared<AttributeValue>();
    av->type = AttributeType::N;
    char buf[64];
    if (n == static_cast<long long>(n) && n < 9.2e18L && n > -9.2e18L) {
        int len = std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
        av->value = core::String(buf, buf + (len > 0 ? len : 0));
    } else {
        int len = std::snprintf(buf, sizeof(buf), "%.17Lg", n);
        av->value = core::String(buf, buf + (len > 0 ? len : 0));
    }
    return av;
}

// ---- Parser / evaluator -------------------------------------------------

struct Applier {
    const std::vector<Tok>& toks;
    size_t pos = 0;
    ItemMap& item;
    const NameMap& names;
    const ValueMap& values;
    std::string error;

    const Tok& peek() const { return toks[pos]; }
    const Tok& advance() { return toks[pos < toks.size() - 1 ? pos++ : pos]; }

    bool fail(std::string msg) {
        if (error.empty()) error = std::move(msg);
        return false;
    }

    // Resolves a path token (Ident or #Name) to a top-level attribute name.
    std::optional<std::string> resolve_path() {
        const Tok& t = peek();
        if (t.type == TT::Ident) { advance(); return t.text; }
        if (t.type == TT::Name) {
            advance();
            auto it = names.find(t.text);
            if (it == names.end()) { fail("ExpressionAttributeNames missing entry for " + t.text); return std::nullopt; }
            return it->second;
        }
        fail("Expected an attribute path in UpdateExpression");
        return std::nullopt;
    }

    std::shared_ptr<AttributeValue> resolve_value_ref() {
        const Tok& t = peek();
        if (t.type != TT::Value) { fail("Expected a value placeholder"); return nullptr; }
        advance();
        auto it = values.find(t.text);
        if (it == values.end()) { fail("ExpressionAttributeValues missing entry for " + t.text); return nullptr; }
        return it->second;
    }

    // operand := value | path | if_not_exists(path, operand) | list_append(operand, operand)
    std::shared_ptr<AttributeValue> parse_operand() {
        const Tok& t = peek();
        if (t.type == TT::Value) return resolve_value_ref();
        if (t.type == TT::Ident) {
            std::string lname = upper(t.text);
            if (lname == "IF_NOT_EXISTS" && toks[pos + 1].type == TT::LParen) {
                advance();  // function name
                advance();  // (
                auto path = resolve_path();
                if (!path) return nullptr;
                if (peek().type != TT::Comma) { fail("if_not_exists expects two arguments"); return nullptr; }
                advance();
                auto fallback = parse_additive();
                if (peek().type != TT::RParen) { fail("if_not_exists missing )"); return nullptr; }
                advance();
                auto existing = item.find(*path);
                if (existing != item.end() && existing->second) return existing->second;
                return fallback;
            }
            if (lname == "LIST_APPEND" && toks[pos + 1].type == TT::LParen) {
                advance();  // function name
                advance();  // (
                auto a = parse_additive();
                if (peek().type != TT::Comma) { fail("list_append expects two arguments"); return nullptr; }
                advance();
                auto b = parse_additive();
                if (peek().type != TT::RParen) { fail("list_append missing )"); return nullptr; }
                advance();
                if (!a || !b || a->type != AttributeType::L || b->type != AttributeType::L) {
                    fail("list_append requires two list operands");
                    return nullptr;
                }
                auto result = std::make_shared<AttributeValue>();
                result->type = AttributeType::L;
                core::ListValue merged = std::get<core::ListValue>(a->value);
                for (const auto& e : std::get<core::ListValue>(b->value)) merged.push_back(e);
                result->value = std::move(merged);
                return result;
            }
            // Plain path operand.
            auto path = resolve_path();
            if (!path) return nullptr;
            auto existing = item.find(*path);
            if (existing != item.end()) return existing->second;
            return nullptr;  // missing operand
        }
        if (t.type == TT::Name) {
            auto path = resolve_path();
            if (!path) return nullptr;
            auto existing = item.find(*path);
            if (existing != item.end()) return existing->second;
            return nullptr;
        }
        fail("Unexpected token in UpdateExpression operand");
        return nullptr;
    }

    // additive := operand (('+'|'-') operand)*
    std::shared_ptr<AttributeValue> parse_additive() {
        auto left = parse_operand();
        while (peek().type == TT::Op && (peek().text == "+" || peek().text == "-")) {
            std::string op = advance().text;
            auto right = parse_operand();
            if (!left || !right || left->type != AttributeType::N || right->type != AttributeType::N) {
                fail("Arithmetic in SET requires numeric operands");
                return nullptr;
            }
            long double l = 0;
            long double r = 0;
            if (!parse_num(std::get<core::String>(left->value), l) ||
                !parse_num(std::get<core::String>(right->value), r)) {
                fail("Invalid number in SET arithmetic");
                return nullptr;
            }
            left = make_number(op == "+" ? l + r : l - r);
        }
        return left;
    }

    bool do_set() {
        while (true) {
            auto target = resolve_path();
            if (!target) return false;
            if (!(peek().type == TT::Op && peek().text == "=")) return fail("SET clause expects '='");
            advance();
            auto value = parse_additive();
            if (!error.empty()) return false;
            if (!value) return fail("SET value resolves to nothing");
            item[*target] = value;
            if (peek().type == TT::Comma) { advance(); continue; }
            break;
        }
        return true;
    }

    bool do_remove() {
        while (true) {
            auto target = resolve_path();
            if (!target) return false;
            item.erase(*target);
            if (peek().type == TT::Comma) { advance(); continue; }
            break;
        }
        return true;
    }

    bool do_add() {
        while (true) {
            auto target = resolve_path();
            if (!target) return false;
            auto operand = resolve_value_ref();
            if (!operand) return false;
            auto existing = item.find(*target);
            if (operand->type == AttributeType::N) {
                long double delta = 0;
                if (!parse_num(std::get<core::String>(operand->value), delta)) return fail("ADD value is not a number");
                long double base = 0;
                if (existing != item.end() && existing->second && existing->second->type == AttributeType::N) {
                    parse_num(std::get<core::String>(existing->second->value), base);
                }
                item[*target] = make_number(base + delta);
            } else if (operand->type == AttributeType::SS || operand->type == AttributeType::NS) {
                // Union into an existing set of the same type (or create it).
                if (existing == item.end() || !existing->second) {
                    item[*target] = operand;
                } else if (existing->second->type == operand->type) {
                    auto merged = std::make_shared<AttributeValue>(*existing->second);
                    if (operand->type == AttributeType::SS) {
                        auto& dst = std::get<core::StringSet>(merged->value).values;
                        for (const auto& v : std::get<core::StringSet>(operand->value).values) {
                            if (std::find(dst.begin(), dst.end(), v) == dst.end()) dst.push_back(v);
                        }
                    } else {
                        auto& dst = std::get<core::NumberSet>(merged->value).values;
                        for (const auto& v : std::get<core::NumberSet>(operand->value).values) {
                            if (std::find(dst.begin(), dst.end(), v) == dst.end()) dst.push_back(v);
                        }
                    }
                    item[*target] = merged;
                } else {
                    return fail("ADD set type does not match existing attribute");
                }
            } else {
                return fail("ADD supports only Number and Set values");
            }
            if (peek().type == TT::Comma) { advance(); continue; }
            break;
        }
        return true;
    }

    bool do_delete() {
        while (true) {
            auto target = resolve_path();
            if (!target) return false;
            auto operand = resolve_value_ref();
            if (!operand) return false;
            auto existing = item.find(*target);
            if (existing != item.end() && existing->second &&
                existing->second->type == operand->type &&
                (operand->type == AttributeType::SS || operand->type == AttributeType::NS)) {
                auto result = std::make_shared<AttributeValue>(*existing->second);
                if (operand->type == AttributeType::SS) {
                    auto& dst = std::get<core::StringSet>(result->value).values;
                    const auto& rm = std::get<core::StringSet>(operand->value).values;
                    dst.erase(std::remove_if(dst.begin(), dst.end(), [&](const core::String& s) {
                                  return std::find(rm.begin(), rm.end(), s) != rm.end();
                              }), dst.end());
                    if (dst.empty()) item.erase(*target); else item[*target] = result;
                } else {
                    auto& dst = std::get<core::NumberSet>(result->value).values;
                    const auto& rm = std::get<core::NumberSet>(operand->value).values;
                    dst.erase(std::remove_if(dst.begin(), dst.end(), [&](const core::String& s) {
                                  return std::find(rm.begin(), rm.end(), s) != rm.end();
                              }), dst.end());
                    if (dst.empty()) item.erase(*target); else item[*target] = result;
                }
            }
            // DELETE on a missing/typed-mismatched attribute is a no-op in AWS.
            if (peek().type == TT::Comma) { advance(); continue; }
            break;
        }
        return true;
    }

    bool run() {
        if (peek().type == TT::End) return fail("UpdateExpression is empty");
        while (peek().type == TT::Keyword) {
            std::string section = advance().text;
            bool ok = false;
            if (section == "SET") ok = do_set();
            else if (section == "REMOVE") ok = do_remove();
            else if (section == "ADD") ok = do_add();
            else if (section == "DELETE") ok = do_delete();
            if (!ok) return false;
        }
        if (peek().type != TT::End) return fail("Unexpected trailing tokens in UpdateExpression");
        return true;
    }
};

}  // namespace

UpdateResult apply_update_expression(const std::string& expression, ItemMap& item,
                                     const NameMap& names, const ValueMap& values) {
    bool bad = false;
    auto toks = tokenize(expression, bad);
    if (bad) return {false, "Invalid UpdateExpression syntax"};
    // Operate on a copy so a mid-expression failure leaves the item untouched.
    ItemMap working = item;
    Applier ap{toks, 0, working, names, values, {}};
    if (!ap.run()) return {false, ap.error.empty() ? "Invalid UpdateExpression" : ap.error};
    item = std::move(working);
    return {true, {}};
}

}  // namespace cynamodb::expressions
