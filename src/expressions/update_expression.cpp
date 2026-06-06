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
// The shared expression Lexer does not emit '+'/'-', and UpdateExpressions need
// them, so update expressions get their own small tokenizer. It also emits Dot
// and Index tokens for document paths (a.b, a[0]).

enum class TT { Ident, Name, Value, Op, LParen, RParen, Comma, Dot, Index, Keyword, End, Bad };

struct Tok {
    TT type;
    std::string text;
    size_t index = 0;  // for TT::Index
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
            out.push_back({TT::Name, in.substr(start, i - start), 0});
        } else if (c == ':') {
            size_t start = i++;
            if (i >= in.size() || !ident_start(static_cast<unsigned char>(in[i]))) { bad = true; break; }
            while (i < in.size() && ident_char(static_cast<unsigned char>(in[i]))) ++i;
            out.push_back({TT::Value, in.substr(start, i - start), 0});
        } else if (ident_start(static_cast<unsigned char>(c))) {
            size_t start = i;
            while (i < in.size() && (ident_char(static_cast<unsigned char>(in[i])))) ++i;
            std::string word = in.substr(start, i - start);
            if (is_section_keyword(upper(word))) out.push_back({TT::Keyword, upper(word), 0});
            else out.push_back({TT::Ident, word, 0});
        } else if (c == '=' || c == '+' || c == '-') {
            out.push_back({TT::Op, std::string(1, c), 0});
            ++i;
        } else if (c == '(') { out.push_back({TT::LParen, "(", 0}); ++i; }
        else if (c == ')') { out.push_back({TT::RParen, ")", 0}); ++i; }
        else if (c == ',') { out.push_back({TT::Comma, ",", 0}); ++i; }
        else if (c == '.') { out.push_back({TT::Dot, ".", 0}); ++i; }
        else if (c == '[') {
            size_t start = ++i;
            while (i < in.size() && std::isdigit(static_cast<unsigned char>(in[i])) != 0) ++i;
            if (i == start || i >= in.size() || in[i] != ']') { bad = true; break; }
            std::string digits = in.substr(start, i - start);
            ++i;  // consume ']'
            size_t idx = 0;
            try { idx = static_cast<size_t>(std::stoul(digits)); } catch (...) { bad = true; break; }
            out.push_back({TT::Index, "", idx});
        } else { bad = true; break; }
    }
    out.push_back({TT::End, "", 0});
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

// Deep-copies an attribute value so a nested mutation never aliases the value
// objects shared with the stored item (working maps copy the shared_ptrs, not
// the pointees).
std::shared_ptr<AttributeValue> deep_clone(const std::shared_ptr<AttributeValue>& v) {
    if (!v) return nullptr;
    auto out = std::make_shared<AttributeValue>();
    out->type = v->type;
    switch (v->type) {
        case AttributeType::M: {
            core::MapValue m;
            for (const auto& [k, child] : std::get<core::MapValue>(v->value)) m[k] = deep_clone(child);
            out->value = std::move(m);
            break;
        }
        case AttributeType::L: {
            core::ListValue l;
            for (const auto& child : std::get<core::ListValue>(v->value)) l.push_back(deep_clone(child));
            out->value = std::move(l);
            break;
        }
        default:
            out->value = v->value;  // scalars and sets are value types
            break;
    }
    return out;
}

// ---- Document path ------------------------------------------------------

struct Seg { bool is_index = false; std::string name; size_t index = 0; };
using Path = std::vector<Seg>;

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

    std::optional<std::string> resolve_name(const std::string& raw) {
        if (raw.starts_with("#")) {
            auto it = names.find(raw);
            if (it == names.end()) { fail("ExpressionAttributeNames missing entry for " + raw); return std::nullopt; }
            return it->second;
        }
        return raw;
    }

    // Parses a document path: root (Ident|#Name) followed by .name / [index] steps.
    std::optional<Path> parse_path() {
        const Tok& t = peek();
        if (t.type != TT::Ident && t.type != TT::Name) { fail("Expected an attribute path"); return std::nullopt; }
        advance();
        auto root = resolve_name(t.text);
        if (!root) return std::nullopt;
        Path p;
        p.push_back({false, *root, 0});
        while (peek().type == TT::Dot || peek().type == TT::Index) {
            if (peek().type == TT::Dot) {
                advance();
                const Tok& seg = peek();
                if (seg.type != TT::Ident && seg.type != TT::Name) { fail("Expected a name after '.'"); return std::nullopt; }
                advance();
                auto n = resolve_name(seg.text);
                if (!n) return std::nullopt;
                p.push_back({false, *n, 0});
            } else {
                size_t idx = advance().index;
                p.push_back({true, "", idx});
            }
        }
        return p;
    }

    std::shared_ptr<AttributeValue> resolve_value_ref() {
        const Tok& t = peek();
        if (t.type != TT::Value) { fail("Expected a value placeholder"); return nullptr; }
        advance();
        auto it = values.find(t.text);
        if (it == values.end()) { fail("ExpressionAttributeValues missing entry for " + t.text); return nullptr; }
        return it->second;
    }

    // Reads the current value at a path (nullptr if absent).
    std::shared_ptr<AttributeValue> read_path(const Path& p) {
        if (p.empty() || p[0].is_index) return nullptr;
        auto it = item.find(p[0].name);
        if (it == item.end()) return nullptr;
        std::shared_ptr<AttributeValue> cur = it->second;
        for (size_t i = 1; i < p.size() && cur; ++i) {
            const auto& s = p[i];
            if (s.is_index) {
                if (cur->type != AttributeType::L) return nullptr;
                const auto& l = std::get<core::ListValue>(cur->value);
                if (s.index >= l.size()) return nullptr;
                cur = l[s.index];
            } else {
                if (cur->type != AttributeType::M) return nullptr;
                const auto& m = std::get<core::MapValue>(cur->value);
                auto mit = m.find(core::String(s.name));
                if (mit == m.end()) return nullptr;
                cur = mit->second;
            }
        }
        return cur;
    }

    // Writes `value` at a path (set). Parents must already exist; a list index may
    // equal the list size (append). Uses copy-on-write on the root attribute.
    bool write_path(const Path& p, const std::shared_ptr<AttributeValue>& value) {
        if (p.empty() || p[0].is_index) return fail("Invalid update path");
        if (p.size() == 1) { item[p[0].name] = value; return true; }

        auto it = item.find(p[0].name);
        if (it == item.end() || !it->second) return fail("Update path parent does not exist");
        auto root = deep_clone(it->second);
        AttributeValue* cur = root.get();
        for (size_t i = 1; i + 1 < p.size(); ++i) {  // navigate to the parent of the leaf
            const auto& s = p[i];
            if (s.is_index) {
                if (cur->type != AttributeType::L) return fail("Update path is not a list");
                auto& l = std::get<core::ListValue>(cur->value);
                if (s.index >= l.size() || !l[s.index]) return fail("Update path index out of range");
                cur = l[s.index].get();
            } else {
                if (cur->type != AttributeType::M) return fail("Update path is not a map");
                auto& m = std::get<core::MapValue>(cur->value);
                auto mit = m.find(core::String(s.name));
                if (mit == m.end() || !mit->second) return fail("Update path parent does not exist");
                cur = mit->second.get();
            }
        }
        const auto& leaf = p.back();
        if (leaf.is_index) {
            if (cur->type != AttributeType::L) return fail("Update path is not a list");
            auto& l = std::get<core::ListValue>(cur->value);
            if (leaf.index < l.size()) l[leaf.index] = value;
            else if (leaf.index == l.size()) l.push_back(value);
            else return fail("Update list index out of range");
        } else {
            if (cur->type != AttributeType::M) return fail("Update path is not a map");
            std::get<core::MapValue>(cur->value)[core::String(leaf.name)] = value;
        }
        item[p[0].name] = root;
        return true;
    }

    bool erase_path(const Path& p) {
        if (p.empty() || p[0].is_index) return true;
        if (p.size() == 1) { item.erase(p[0].name); return true; }
        auto it = item.find(p[0].name);
        if (it == item.end() || !it->second) return true;  // nothing to remove
        auto root = deep_clone(it->second);
        AttributeValue* cur = root.get();
        for (size_t i = 1; i + 1 < p.size(); ++i) {
            const auto& s = p[i];
            if (s.is_index) {
                if (cur->type != AttributeType::L) return true;
                auto& l = std::get<core::ListValue>(cur->value);
                if (s.index >= l.size() || !l[s.index]) return true;
                cur = l[s.index].get();
            } else {
                if (cur->type != AttributeType::M) return true;
                auto& m = std::get<core::MapValue>(cur->value);
                auto mit = m.find(core::String(s.name));
                if (mit == m.end() || !mit->second) return true;
                cur = mit->second.get();
            }
        }
        const auto& leaf = p.back();
        if (leaf.is_index) {
            if (cur->type != AttributeType::L) return true;
            auto& l = std::get<core::ListValue>(cur->value);
            if (leaf.index < l.size()) l.erase(l.begin() + static_cast<long>(leaf.index));
        } else {
            if (cur->type == AttributeType::M) std::get<core::MapValue>(cur->value).erase(core::String(leaf.name));
        }
        item[p[0].name] = root;
        return true;
    }

    // operand := value | path | if_not_exists(path, operand) | list_append(operand, operand)
    std::shared_ptr<AttributeValue> parse_operand() {
        const Tok& t = peek();
        if (t.type == TT::Value) return resolve_value_ref();
        if (t.type == TT::Ident) {
            std::string lname = upper(t.text);
            if (lname == "IF_NOT_EXISTS" && toks[pos + 1].type == TT::LParen) {
                advance(); advance();  // name + '('
                auto path = parse_path();
                if (!path) return nullptr;
                if (peek().type != TT::Comma) { fail("if_not_exists expects two arguments"); return nullptr; }
                advance();
                auto fallback = parse_additive();
                if (peek().type != TT::RParen) { fail("if_not_exists missing )"); return nullptr; }
                advance();
                auto existing = read_path(*path);
                return existing ? existing : fallback;
            }
            if (lname == "LIST_APPEND" && toks[pos + 1].type == TT::LParen) {
                advance(); advance();  // name + '('
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
            auto path = parse_path();
            if (!path) return nullptr;
            return read_path(*path);
        }
        if (t.type == TT::Name) {
            auto path = parse_path();
            if (!path) return nullptr;
            return read_path(*path);
        }
        fail("Unexpected token in UpdateExpression operand");
        return nullptr;
    }

    std::shared_ptr<AttributeValue> parse_additive() {
        auto left = parse_operand();
        while (peek().type == TT::Op && (peek().text == "+" || peek().text == "-")) {
            std::string op = advance().text;
            auto right = parse_operand();
            if (!left || !right || left->type != AttributeType::N || right->type != AttributeType::N) {
                fail("Arithmetic in SET requires numeric operands");
                return nullptr;
            }
            long double l = 0, r = 0;
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
            auto target = parse_path();
            if (!target) return false;
            if (!(peek().type == TT::Op && peek().text == "=")) return fail("SET clause expects '='");
            advance();
            auto value = parse_additive();
            if (!error.empty()) return false;
            if (!value) return fail("SET value resolves to nothing");
            if (!write_path(*target, value)) return false;
            if (peek().type == TT::Comma) { advance(); continue; }
            break;
        }
        return true;
    }

    bool do_remove() {
        while (true) {
            auto target = parse_path();
            if (!target) return false;
            if (!erase_path(*target)) return false;
            if (peek().type == TT::Comma) { advance(); continue; }
            break;
        }
        return true;
    }

    bool do_add() {
        while (true) {
            auto target = parse_path();
            if (!target) return false;
            auto operand = resolve_value_ref();
            if (!operand) return false;
            auto existing = read_path(*target);
            if (operand->type == AttributeType::N) {
                long double delta = 0;
                if (!parse_num(std::get<core::String>(operand->value), delta)) return fail("ADD value is not a number");
                long double base = 0;
                if (existing && existing->type == AttributeType::N) parse_num(std::get<core::String>(existing->value), base);
                if (!write_path(*target, make_number(base + delta))) return false;
            } else if (operand->type == AttributeType::SS || operand->type == AttributeType::NS) {
                if (!existing) {
                    if (!write_path(*target, operand)) return false;
                } else if (existing->type == operand->type) {
                    auto merged = std::make_shared<AttributeValue>(*existing);
                    if (operand->type == AttributeType::SS) {
                        auto& dst = std::get<core::StringSet>(merged->value).values;
                        for (const auto& v : std::get<core::StringSet>(operand->value).values)
                            if (std::find(dst.begin(), dst.end(), v) == dst.end()) dst.push_back(v);
                    } else {
                        auto& dst = std::get<core::NumberSet>(merged->value).values;
                        for (const auto& v : std::get<core::NumberSet>(operand->value).values)
                            if (std::find(dst.begin(), dst.end(), v) == dst.end()) dst.push_back(v);
                    }
                    if (!write_path(*target, merged)) return false;
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
            auto target = parse_path();
            if (!target) return false;
            auto operand = resolve_value_ref();
            if (!operand) return false;
            auto existing = read_path(*target);
            if (existing && existing->type == operand->type &&
                (operand->type == AttributeType::SS || operand->type == AttributeType::NS)) {
                auto result = std::make_shared<AttributeValue>(*existing);
                bool empty = false;
                if (operand->type == AttributeType::SS) {
                    auto& dst = std::get<core::StringSet>(result->value).values;
                    const auto& rm = std::get<core::StringSet>(operand->value).values;
                    dst.erase(std::remove_if(dst.begin(), dst.end(), [&](const core::String& s) {
                                  return std::find(rm.begin(), rm.end(), s) != rm.end();
                              }), dst.end());
                    empty = dst.empty();
                } else {
                    auto& dst = std::get<core::NumberSet>(result->value).values;
                    const auto& rm = std::get<core::NumberSet>(operand->value).values;
                    dst.erase(std::remove_if(dst.begin(), dst.end(), [&](const core::String& s) {
                                  return std::find(rm.begin(), rm.end(), s) != rm.end();
                              }), dst.end());
                    empty = dst.empty();
                }
                if (empty) { if (!erase_path(*target)) return false; }
                else { if (!write_path(*target, result)) return false; }
            }
            // DELETE on a missing / type-mismatched attribute is a no-op in AWS.
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
    ItemMap working = item;  // operate on a copy so a mid-expression failure is atomic
    Applier ap{toks, 0, working, names, values, {}};
    if (!ap.run()) return {false, ap.error.empty() ? "Invalid UpdateExpression" : ap.error};
    item = std::move(working);
    return {true, {}};
}

}  // namespace cynamodb::expressions
