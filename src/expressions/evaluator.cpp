#include <cynamodb/expressions/evaluator.hpp>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace cynamodb::expressions {

namespace {

bool is_valid_identifier(std::string_view value) {
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_' || c == '-' || c == '.';
    });
}

// Parses a DynamoDB number string for ordered comparison. long double preserves
// more integer precision than double; values beyond its range compare by string
// length+lexicographic as a best effort.
bool parse_number(std::string_view s, long double& out) {
    std::string tmp(s);
    char* end = nullptr;
    errno = 0;
    out = std::strtold(tmp.c_str(), &end);
    return end == tmp.c_str() + tmp.size() && !tmp.empty();
}

bool compare_numeric(std::string_view l, std::string_view r, std::string_view op) {
    long double ln = 0;
    long double rn = 0;
    if (!parse_number(l, ln) || !parse_number(r, rn)) {
        // Fall back to exact string compare for un-parseable numbers.
        if (op == "=") return l == r;
        if (op == "<>") return l != r;
        return false;
    }
    if (op == "=") return ln == rn;
    if (op == "<>") return ln != rn;
    if (op == "<") return ln < rn;
    if (op == "<=") return ln <= rn;
    if (op == ">") return ln > rn;
    if (op == ">=") return ln >= rn;
    return false;
}

std::shared_ptr<core::AttributeValue> make_number(long double n) {
    auto av = std::make_shared<core::AttributeValue>();
    av->type = core::AttributeType::N;
    // Render without trailing zeros for integral values.
    char buf[64];
    if (n == static_cast<long long>(n) && n < 9.2e18L && n > -9.2e18L) {
        auto v = static_cast<long long>(n);
        auto res = std::to_chars(buf, buf + sizeof(buf), v);
        av->value = core::String(buf, res.ptr);
    } else {
        int len = std::snprintf(buf, sizeof(buf), "%.17Lg", n);
        av->value = core::String(buf, buf + (len > 0 ? len : 0));
    }
    return av;
}

using ItemMap = std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>;
using NameMap = std::map<std::string, std::string, core::StringViewLess>;

// Resolves a path segment's name, dereferencing a leading '#' via ExpressionAttributeNames.
std::optional<std::string> resolve_segment_name(const std::string& raw, const NameMap& names) {
    if (raw.starts_with("#")) {
        auto it = names.find(raw);
        if (it == names.end() || !is_valid_identifier(it->second)) return std::nullopt;
        return it->second;
    }
    return raw;
}

// Walks a document path (a, a.b.c, a[0].b, #a.#b[2]) into the item, descending
// into maps for Name steps and lists for Index steps. Returns nullptr if any step
// does not resolve (missing key, out-of-range index, type mismatch).
std::shared_ptr<core::AttributeValue> navigate_path(const PathNode& path, const ItemMap& item,
                                                   const NameMap& names) {
    if (path.segments.empty()) return nullptr;
    const auto& root = path.segments.front();
    if (root.kind != PathSegment::Kind::Name) return nullptr;
    auto root_name = resolve_segment_name(root.name, names);
    if (!root_name) return nullptr;
    auto it = item.find(*root_name);
    if (it == item.end()) return nullptr;
    std::shared_ptr<core::AttributeValue> current = it->second;

    for (size_t i = 1; i < path.segments.size() && current; ++i) {
        const auto& seg = path.segments[i];
        if (seg.kind == PathSegment::Kind::Name) {
            if (current->type != core::AttributeType::M) return nullptr;
            auto name = resolve_segment_name(seg.name, names);
            if (!name) return nullptr;
            const auto& m = std::get<core::MapValue>(current->value);
            auto mit = m.find(core::String(*name));
            if (mit == m.end()) return nullptr;
            current = mit->second;
        } else {  // Index
            if (current->type != core::AttributeType::L) return nullptr;
            const auto& l = std::get<core::ListValue>(current->value);
            if (seg.index >= l.size()) return nullptr;
            current = l[seg.index];
        }
    }
    return current;
}

std::string type_code(core::AttributeType t) {
    switch (t) {
        case core::AttributeType::S: return "S";
        case core::AttributeType::N: return "N";
        case core::AttributeType::B: return "B";
        case core::AttributeType::BOOL: return "BOOL";
        case core::AttributeType::NUL: return "NULL";
        case core::AttributeType::M: return "M";
        case core::AttributeType::L: return "L";
        case core::AttributeType::SS: return "SS";
        case core::AttributeType::NS: return "NS";
        case core::AttributeType::BS: return "BS";
    }
    return "";
}

std::string to_upper_copy(std::string_view input) {
    std::string out(input);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return out;
}

bool compare_values(
    const std::shared_ptr<core::AttributeValue>& left,
    const std::shared_ptr<core::AttributeValue>& right,
    std::string_view op) {
    if (!left || !right) return false;

    if (left->type == core::AttributeType::S && right->type == core::AttributeType::S) {
        const auto& l = std::get<core::String>(left->value);
        const auto& r = std::get<core::String>(right->value);
        if (op == "=") return l == r;
        if (op == "<>") return l != r;
        if (op == "<") return l < r;
        if (op == "<=") return l <= r;
        if (op == ">") return l > r;
        if (op == ">=") return l >= r;
        return false;
    }

    if (left->type == core::AttributeType::N && right->type == core::AttributeType::N) {
        return compare_numeric(std::get<core::String>(left->value),
                               std::get<core::String>(right->value), op);
    }

    if (left->type == core::AttributeType::B && right->type == core::AttributeType::B) {
        const auto& l = std::get<std::pmr::vector<uint8_t>>(left->value);
        const auto& r = std::get<std::pmr::vector<uint8_t>>(right->value);
        if (op == "=") return l == r;
        if (op == "<>") return l != r;
        if (op == "<") return l < r;
        if (op == "<=") return l <= r;
        if (op == ">") return l > r;
        if (op == ">=") return l >= r;
        return false;
    }

    if (left->type == core::AttributeType::BOOL && right->type == core::AttributeType::BOOL) {
        const bool l = std::get<bool>(left->value);
        const bool r = std::get<bool>(right->value);
        if (op == "=") return l == r;
        if (op == "<>") return l != r;
        return false;
    }

    if (left->type == core::AttributeType::NUL && right->type == core::AttributeType::NUL) {
        if (op == "=") return true;
        if (op == "<>") return false;
    }

    return false;
}

} // namespace

bool Evaluator::evaluate_condition(
    const ASTNode& node,
    const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item,
    const std::map<std::string, std::string, core::StringViewLess>& names,
    const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& values) {
    return evaluate_condition_impl(node, item, names, values, 0);
}

bool Evaluator::evaluate_condition_impl(
    const ASTNode& node,
    const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item,
    const std::map<std::string, std::string, core::StringViewLess>& names,
    const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& values,
    size_t depth) {
    if (depth > 128) return false;

    if (std::holds_alternative<BinaryOpNode>(node.data)) {
        const auto& bin = std::get<BinaryOpNode>(node.data);
        // Gate 4: Short-Circuit Evaluation
        if (bin.op == "AND") {
            if (!evaluate_condition_impl(*bin.left, item, names, values, depth + 1)) return false;
            return evaluate_condition_impl(*bin.right, item, names, values, depth + 1);
        }
        if (bin.op == "OR") {
            if (evaluate_condition_impl(*bin.left, item, names, values, depth + 1)) return true;
            return evaluate_condition_impl(*bin.right, item, names, values, depth + 1);
        }
        
        auto left_val = evaluate_expression(*bin.left, item, names, values);
        auto right_val = evaluate_expression(*bin.right, item, names, values);
        return compare_values(left_val, right_val, bin.op);
    }

    if (std::holds_alternative<FunctionCallNode>(node.data)) {
        const auto& func = std::get<FunctionCallNode>(node.data);
        const std::string fn = to_upper_copy(func.function_name);

        if (fn == "NOT" && func.arguments.size() == 1) {
            return !evaluate_condition_impl(*func.arguments[0], item, names, values, depth + 1);
        }

        if ((fn == "ATTRIBUTE_EXISTS" || fn == "ATTRIBUTE_NOT_EXISTS") && func.arguments.size() == 1) {
            // Path-aware: a value resolves iff the (possibly nested) attribute exists.
            bool exists = evaluate_expression(*func.arguments[0], item, names, values) != nullptr;
            return (fn == "ATTRIBUTE_EXISTS") ? exists : !exists;
        }

        if (fn == "BEGINS_WITH" && func.arguments.size() == 2) {
            auto val = evaluate_expression(*func.arguments[0], item, names, values);
            auto prefix = evaluate_expression(*func.arguments[1], item, names, values);
            if (!val || !prefix) return false;
            if (val->type == core::AttributeType::S && prefix->type == core::AttributeType::S) {
                return std::get<core::String>(val->value).starts_with(std::get<core::String>(prefix->value));
            }
            if (val->type == core::AttributeType::B && prefix->type == core::AttributeType::B) {
                const auto& v = std::get<std::pmr::vector<uint8_t>>(val->value);
                const auto& p = std::get<std::pmr::vector<uint8_t>>(prefix->value);
                return v.size() >= p.size() && std::equal(p.begin(), p.end(), v.begin());
            }
            return false;
        }

        if (fn == "CONTAINS" && func.arguments.size() == 2) {
            auto val = evaluate_expression(*func.arguments[0], item, names, values);
            auto operand = evaluate_expression(*func.arguments[1], item, names, values);
            if (!val || !operand) return false;
            if (val->type == core::AttributeType::S && operand->type == core::AttributeType::S) {
                return std::get<core::String>(val->value).find(std::get<core::String>(operand->value)) !=
                       core::String::npos;
            }
            if (val->type == core::AttributeType::SS && operand->type == core::AttributeType::S) {
                const auto& set = std::get<core::StringSet>(val->value).values;
                return std::find(set.begin(), set.end(), std::get<core::String>(operand->value)) != set.end();
            }
            if (val->type == core::AttributeType::NS && operand->type == core::AttributeType::N) {
                const auto& set = std::get<core::NumberSet>(val->value).values;
                return std::find(set.begin(), set.end(), std::get<core::String>(operand->value)) != set.end();
            }
            return false;
        }

        if (fn == "ATTRIBUTE_TYPE" && func.arguments.size() == 2) {
            auto val = evaluate_expression(*func.arguments[0], item, names, values);
            auto type = evaluate_expression(*func.arguments[1], item, names, values);
            if (!val || !type || type->type != core::AttributeType::S) return false;
            const auto& want = std::get<core::String>(type->value);
            return type_code(val->type) == std::string_view(want.data(), want.size());
        }

        if (fn == "BETWEEN" && func.arguments.size() == 3) {
            auto v = evaluate_expression(*func.arguments[0], item, names, values);
            auto lo = evaluate_expression(*func.arguments[1], item, names, values);
            auto hi = evaluate_expression(*func.arguments[2], item, names, values);
            return compare_values(v, lo, ">=") && compare_values(v, hi, "<=");
        }

        if (fn == "IN" && func.arguments.size() >= 2) {
            auto v = evaluate_expression(*func.arguments[0], item, names, values);
            for (size_t i = 1; i < func.arguments.size(); ++i) {
                auto candidate = evaluate_expression(*func.arguments[i], item, names, values);
                if (compare_values(v, candidate, "=")) return true;
            }
            return false;
        }
    }

    return false;
}

std::shared_ptr<core::AttributeValue> Evaluator::evaluate_expression(
    const ASTNode& node,
    const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item,
    const std::map<std::string, std::string, core::StringViewLess>& names,
    const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& values) {
    
    if (std::holds_alternative<LiteralNode>(node.data)) {
        return std::get<LiteralNode>(node.data).value;
    }

    if (std::holds_alternative<FunctionCallNode>(node.data)) {
        const auto& func = std::get<FunctionCallNode>(node.data);
        std::string fn = func.function_name;
        std::transform(fn.begin(), fn.end(), fn.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        if (fn == "SIZE" && func.arguments.size() == 1) {
            auto val = evaluate_expression(*func.arguments[0], item, names, values);
            if (!val) return nullptr;
            switch (val->type) {
                case core::AttributeType::S: return make_number(static_cast<long double>(std::get<core::String>(val->value).size()));
                case core::AttributeType::B: return make_number(static_cast<long double>(std::get<std::pmr::vector<uint8_t>>(val->value).size()));
                case core::AttributeType::L: return make_number(static_cast<long double>(std::get<core::ListValue>(val->value).size()));
                case core::AttributeType::M: return make_number(static_cast<long double>(std::get<core::MapValue>(val->value).size()));
                case core::AttributeType::SS: return make_number(static_cast<long double>(std::get<core::StringSet>(val->value).values.size()));
                case core::AttributeType::NS: return make_number(static_cast<long double>(std::get<core::NumberSet>(val->value).values.size()));
                case core::AttributeType::BS: return make_number(static_cast<long double>(std::get<core::BinarySet>(val->value).values.size()));
                default: return nullptr;
            }
        }
        return nullptr;
    }

    if (std::holds_alternative<PathNode>(node.data)) {
        return navigate_path(std::get<PathNode>(node.data), item, names);
    }

    if (std::holds_alternative<IdentifierNode>(node.data)) {
        const auto& id = std::get<IdentifierNode>(node.data);
        auto it = item.find(id.name);
        if (it != item.end()) return it->second;
        return nullptr;
    }

    if (std::holds_alternative<PlaceholderNode>(node.data)) {
        const auto& ph = std::get<PlaceholderNode>(node.data);
        if (ph.name.starts_with("#")) {
            const auto it = names.find(ph.name);
            if (it == names.end() || !is_valid_identifier(it->second)) {
                return nullptr;
            }
            const auto item_it = item.find(it->second);
            if (item_it != item.end()) return item_it->second;
            return nullptr;
        }
        if (ph.name.starts_with(":")) {
            const auto it = values.find(ph.name);
            if (it != values.end()) return it->second;
            return nullptr;
        }
    }

    return nullptr;
}

std::shared_ptr<core::AttributeValue> Evaluator::resolve_path(
    const PathNode& path,
    const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& item,
    const std::map<std::string, std::string, core::StringViewLess>& names) {
    return navigate_path(path, item, names);
}

std::optional<std::string> Evaluator::get_attribute_name(
    const ASTNode& node,
    const std::map<std::string, std::string, core::StringViewLess>& names) {
    if (std::holds_alternative<PathNode>(node.data)) {
        const auto& p = std::get<PathNode>(node.data);
        if (p.segments.size() == 1 && p.segments[0].kind == PathSegment::Kind::Name) {
            return resolve_segment_name(p.segments[0].name, names);
        }
        return std::nullopt;
    }
    if (std::holds_alternative<IdentifierNode>(node.data)) {
        return std::get<IdentifierNode>(node.data).name;
    }
    if (std::holds_alternative<PlaceholderNode>(node.data)) {
        const auto& ph = std::get<PlaceholderNode>(node.data);
        if (ph.name.starts_with("#")) {
            auto it = names.find(ph.name);
            if (it != names.end()) return it->second;
        }
    }
    return std::nullopt;
}

} // namespace cynamodb::expressions
